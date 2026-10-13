#include "http_tunnel.h"

#include "app_config.h"
#include "socks5_client.h"

#include <Arduino.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <lwip/sockets.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>

/* ---------------- TLS поверх SOCKS5-туннеля ----------------
 * Рукопожатие и обмен — сами на mbedTLS поверх уже открытого fd
 * (socks5_open_tunnel). Проверка сертификата сервера ОТКЛЮЧЕНА
 * (MBEDTLS_SSL_VERIFY_NONE) — по решению владельца устройства.
 * SNI и ALPN http/1.1 задаём обязательно. */

struct TlsTunnel {
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context  entropy;
    int                      fd;
    bool                     up;
};

/* BIO-колбэки mbedTLS: временную неготовность сокета по контракту
 * возвращаем как WANT_READ/WANT_WRITE (не NET_*). */
static int bio_write(void *ctx, const unsigned char *buf, size_t len) {
    const int fd = *(const int *)ctx;
    for (;;) {
        const int n = ::send(fd, buf, len, 0);
        if (n > 0) return n;
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
}

static int bio_read(void *ctx, unsigned char *buf, size_t len) {
    const int fd = *(const int *)ctx;
    for (;;) {
        const int n = ::recv(fd, buf, len, 0);
        if (n >= 0) return n;               /* 0 = сервер закрыл соединение */
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
}

static bool tls_tunnel_begin(TlsTunnel *t, int fd, const char *sni) {
    memset(t, 0, sizeof(*t));
    t->fd = fd;

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_entropy_init(&t->entropy);

    /* Любой выход по ошибке — освобождаем уже созданные контексты,
     * иначе DRBG/entropy утекают в DRAM (каждый POST — повторно). */
    int ret = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                                    NULL, 0);
    if (ret != 0) {
        Serial.printf("[http] ctr_drbg_seed: 0x%04X\n", (unsigned)(-ret));
        goto fail;
    }
    ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        Serial.printf("[http] ssl_config_defaults: 0x%04X\n", (unsigned)(-ret));
        goto fail;
    }
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);

    /* Сертификаты не проверяем (по решению владельца устройства). */
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_NONE);

    /* ALPN http/1.1: без него часть CDN рвёт соединение после ClientHello. */
    static const char *alpn[] = { "http/1.1", NULL };
    mbedtls_ssl_conf_alpn_protocols(&t->conf, alpn);
    ret = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (ret != 0) {
        Serial.printf("[http] ssl_setup: 0x%04X\n", (unsigned)(-ret));
        goto fail;
    }
    mbedtls_ssl_set_hostname(&t->ssl, sni);          /* SNI */
    mbedtls_ssl_set_bio(&t->ssl, &t->fd, bio_write, bio_read, NULL);
    t->up = true;
    return true;

fail:
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    return false;
}

static bool tls_tunnel_handshake(TlsTunnel *t, uint32_t timeoutMs) {
    const uint32_t t0 = millis();
    for (;;) {
        const int ret = mbedtls_ssl_handshake(&t->ssl);
        if (ret == 0) return true;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char buf[120];
            mbedtls_strerror(ret, buf, sizeof(buf));
            Serial.printf("[http] TLS: %s (0x%04X)\n", buf, (unsigned)(-ret));
            return false;
        }
        if (millis() - t0 > timeoutMs) {
            Serial.println("[http] TLS: таймаут рукопожатия");
            return false;
        }
        delay(5);
    }
}

static void tls_tunnel_end(TlsTunnel *t) {
    if (!t->up) return;
    mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    t->up = false;
}

/* Код ответа из первой строки HTTP ("HTTP/1.1 404 Not Found" -> 404) */
static int http_status_of(const char *resp);

/* ---------------- POST через туннель ---------------- */

bool http_post_via_socks5(const char *host, uint16_t port,
                          const char *path, const char *contentType,
                          const char *body, uint32_t timeoutMs,
                          char *resp, size_t respSize) {
    const uint32_t tStart = millis();

    const int fd = socks5_open_tunnel(SOCKS5_HOST, SOCKS5_PORT,
                                      host, port, 10000);
    if (fd < 0) {
        Serial.println("[http] SOCKS5-туннель не открылся");
        return false;
    }

    TlsTunnel tls;
    if (!tls_tunnel_begin(&tls, fd, host)) { ::close(fd); return false; }

    bool ok = false;
    if (tls_tunnel_handshake(&tls, timeoutMs)) {
        /* Запрос целиком в одном буфере: заголовки + тело */
        char req[768];
        const int n = snprintf(req, sizeof(req),
                               "POST %s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "User-Agent: DesktopClock/1.0\r\n"
                               "%s%s%s"
                               "Content-Length: %u\r\n"
                               "Connection: close\r\n"
                               "\r\n%s",
                               path, host,
                               (contentType && *contentType) ? "Content-Type: " : "",
                               (contentType && *contentType) ? contentType : "",
                               (contentType && *contentType) ? "\r\n" : "",
                               (unsigned)strlen(body), body);
        if (n > 0 && n < (int)sizeof(req)) {
            mbedtls_ssl_write(&tls.ssl, (const unsigned char *)req, (size_t)n);

            char buf[1024];
            size_t got = 0;
            while (got < sizeof(buf) - 1) {
                const int r = mbedtls_ssl_read(&tls.ssl, (unsigned char *)buf + got,
                                               sizeof(buf) - 1 - got);
                if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    if (millis() - tStart > timeoutMs) break;
                    delay(5);
                    continue;
                }
                if (r <= 0) break;
                got += (size_t)r;
            }
            buf[got] = '\0';

            const int status = http_status_of(buf);
            ok = (status >= 200 && status < 300);

            if (resp && respSize) {
                strncpy(resp, buf, respSize - 1);
                resp[respSize - 1] = '\0';
            }
            Serial.printf("[http] SOCKS5+TLS %s -> %d (%u Б, %u мс)\n",
                          host, status, (unsigned)got,
                          (unsigned)(millis() - tStart));
        }
    }

    tls_tunnel_end(&tls);
    ::close(fd);
    return ok;
}

/* ---------------- GET по локалке (свой сервер) ---------------- */

bool http_get_plain(const char *host, uint16_t port, const char *path,
                    const char *token, uint32_t timeoutMs,
                    char *resp, size_t respSize) {
    const uint32_t tStart = millis();

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u%s", host, port, path);
    Serial.printf("[http] GET %s (timeout %u ms)...\n", url, (unsigned)timeoutMs);

    /* Сырой TCP-сокет + HTTP/1.1 */
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        Serial.println("[http] socket() failed");
        return false;
    }

    /* TCP_NODELAY: отключаем Nagle для маленьких пакетов */
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    struct timeval tv;
    tv.tv_sec  = (long)(timeoutMs / 1000UL) + 2;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (::inet_pton(AF_INET, host, &sa.sin_addr) != 1 ||
        ::connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        ::close(fd);
        Serial.println("[http] connect() failed");
        return false;
    }

    /* HTTP/1.1 GET (X-Hub-Token — только если токен задан) */
    char tokHdr[96] = "";
    if (token && token[0]) {
        snprintf(tokHdr, sizeof(tokHdr), "X-Hub-Token: %s\r\n", token);
    }

    char req[352];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "User-Agent: curl/8.5.0\r\n"
                     "Accept: */*\r\n"
                     "%s"
                     "Connection: close\r\n"
                     "\r\n",
                     path, host, port, tokHdr);
    if (n <= 0 || n >= (int)sizeof(req)) {
        ::close(fd);
        return false;
    }

    if (::send(fd, req, n, 0) != n) {
        ::close(fd);
        Serial.println("[http] send() failed");
        return false;
    }

    /* Читаем ответ с заголовками */
    char buf[1536];
    size_t got = 0;
    while (got < sizeof(buf) - 1) {
        int r = ::recv(fd, buf + got, sizeof(buf) - 1 - got, 0);
        if (r > 0) {
            got += (size_t)r;
            continue;
        }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (millis() - tStart > timeoutMs) break;
            delay(5);
            continue;
        }
        break;
    }
    buf[got] = '\0';
    ::close(fd);

    /* Парсим статус */
    int status = -1;
    if (strncmp(buf, "HTTP/1.", 7) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) status = atoi(sp + 1);
    }

    /* Пропускаем заголовки — ищем двойной CRLF */
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) body = strstr(buf, "\n\n");
    if (body) body += (body[0] == '\r') ? 4 : 2;  // skip \r\n\r\n or \n\n
    else body = buf;  // fallback

    if (resp && respSize) {
        strncpy(resp, body, respSize - 1);
        resp[respSize - 1] = '\0';
    }

    if (status < 200 || status >= 300) {
        Serial.printf("[http] GET %s -> %d (за %u мс, %u байт)\n", url, status,
                      (unsigned)(millis() - tStart), (unsigned)got);
        return false;
    }
    Serial.printf("[http] GET %s -> %d (за %u мс, %u байт, body=%u)\n", url, status,
                  (unsigned)(millis() - tStart), (unsigned)got, (unsigned)strlen(body));
    return true;
}
int http_status_of(const char *resp) {
    if (!resp || strncmp(resp, "HTTP/1.", 7) != 0) return -1;
    const char *sp = strchr(resp, ' ');
    if (!sp) return -1;
    return atoi(sp + 1);
}
