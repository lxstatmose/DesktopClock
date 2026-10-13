#include "socks5_client.h"

#include <string.h>
#include <errno.h>
#include <lwip/sockets.h>

/* SOCKS5 (RFC 1928), без авторизации.
 * Рукопожатие:
 *   C: 05 01 00                    — версия, 1 метод, no-auth
 *   S: 05 00                       — метод выбран
 *   C: 05 01 00 ATYP ADDR PORT     — CONNECT
 *   S: 05 00 00 ATYP ADDR PORT     — успех */

void Socks5Client::setProxy(const char *host, uint16_t port) {
    _proxyHost = host;
    _proxyPort = port;
}

void Socks5Client::setTimeout(uint32_t ms) {
    _timeoutMs = ms;
    _tcp.setTimeout(ms);
}

/* ---------- Сырой сокет: SOCKS5 CONNECT без обёртки Client ---------- */
/* Нужен для TLS поверх прокси (см. socks5_client.h). Сокет остаётся
 * блокирующим, но с таймаутами recv/send — вызывающий сам ограничивает
 * ожидание по времени. */

static bool fd_write_all(int fd, const uint8_t *buf, size_t len, uint32_t timeoutMs) {
    size_t sent = 0;
    const uint32_t t0 = millis();
    while (sent < len) {
        const int n = ::send(fd, buf + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (millis() - t0 > timeoutMs) return false;
            delay(2);
            continue;
        }
        return false;
    }
    return true;
}

static bool fd_read_exact(int fd, uint8_t *buf, size_t len, uint32_t timeoutMs) {
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < len) {
        const int n = ::recv(fd, buf + got, len - got, 0);
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (millis() - t0 > timeoutMs) return false;
            delay(2);
            continue;
        }
        return false;
    }
    return true;
}

int socks5_open_tunnel(const char *proxyHost, uint16_t proxyPort,
                       const char *host, uint16_t port, uint32_t timeoutMs) {
    if (!proxyHost || !host || !*host) return -1;

    const size_t hlen = strlen(host);
    if (hlen == 0 || hlen > 255) return -1;

    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;

    /* TCP_NODELAY: отключаем Nagle для маленьких пакетов */
    int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    /* Таймауты на операции сокета, чтобы не зависнуть намертво */
    struct timeval tv;
    tv.tv_sec  = (long)(timeoutMs / 1000UL) + 1;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* TCP до прокси — локальный, всегда проходит */
    struct sockaddr_in pa;
    memset(&pa, 0, sizeof(pa));
    pa.sin_family = AF_INET;
    pa.sin_port   = htons(proxyPort);
    if (::inet_pton(AF_INET, proxyHost, &pa.sin_addr) != 1 ||
       ::connect(fd, (struct sockaddr *)&pa, sizeof(pa)) != 0) {
        ::close(fd);
        return -1;
    }

    /* Greeting: 05 01 00 (версия, 1 метод, no-auth) */
    const uint8_t greet[3] = { 0x05, 0x01, 0x00 };
    uint8_t reply[2];
    if (!fd_write_all(fd, greet, 3, timeoutMs) ||
        !fd_read_exact(fd, reply, 2, timeoutMs) ||
        reply[0] != 0x05 || reply[1] != 0x00) {
        ::close(fd);
        return -1;
    }

    /* CONNECT, ATYP=0x03 — доменное имя (SOCKS5h: резолвит прокси) */
    uint8_t req[7 + 255];
    req[0] = 0x05;                     /* VER  */
    req[1] = 0x01;                     /* CMD: CONNECT */
    req[2] = 0x00;                     /* RSV  */
    req[3] = 0x03;                     /* ATYP: доменное имя */
    req[4] = (uint8_t)hlen;
    memcpy(req + 5, host, hlen);
    req[5 + hlen] = (uint8_t)(port >> 8);
    req[6 + hlen] = (uint8_t)(port & 0xFF);

    if (!fd_write_all(fd, req, 7 + hlen, timeoutMs)) {
        ::close(fd);
        return -1;
    }

    /* Ответ: VER REP RSV ATYP BND.ADDR BND.PORT */
    uint8_t head[4];
    if (!fd_read_exact(fd, head, 4, timeoutMs) || head[0] != 0x05 || head[1] != 0x00) {
        ::close(fd);
        return -1;
    }

    uint8_t skip[16];
    switch (head[3]) {
        case 0x01:  /* IPv4 */
            if (!fd_read_exact(fd, skip, 6, timeoutMs)) { ::close(fd); return -1; }
            break;
        case 0x03: { /* доменное имя */
            uint8_t alen;
            if (!fd_read_exact(fd, &alen, 1, timeoutMs)) { ::close(fd); return -1; }
            if (alen > sizeof(skip)) { ::close(fd); return -1; }
            if (!fd_read_exact(fd, skip, (size_t)alen + 2, timeoutMs)) { ::close(fd); return -1; }
            break;
        }
        case 0x04:  /* IPv6 */
            if (!fd_read_exact(fd, skip, 18, timeoutMs)) { ::close(fd); return -1; }
            break;
        default:
            ::close(fd);
            return -1;
    }

    return fd;   /* туннель готов, сокет принадлежит вызывающему */
}

int Socks5Client::connect(const char *host, uint16_t port) {
    if (!_tcp.connect(_proxyHost.c_str(), _proxyPort)) return 0;

    /* Greeting: версия, 1 метод, без авторизации */
    const uint8_t greet[3] = { 0x05, 0x01, 0x00 };
    if (_tcp.write(greet, 3) != 3) { _tcp.stop(); return 0; }

    uint8_t reply[2];
    if (!readFully(reply, 2) || reply[0] != 0x05 || reply[1] != 0x00) {
        _tcp.stop();
        return 0;
    }

    /* CONNECT, ATYP=0x03 — доменное имя: DNS разрешает прокси */
    const size_t hlen = strlen(host);
    if (hlen == 0 || hlen > 255) { _tcp.stop(); return 0; }

    const size_t reqlen = 7 + hlen;
    uint8_t *req = (uint8_t *)malloc(reqlen);
    if (!req) { _tcp.stop(); return 0; }

    req[0] = 0x05;                     /* VER */
    req[1] = 0x01;                     /* CMD: CONNECT */
    req[2] = 0x00;                     /* RSV */
    req[3] = 0x03;                     /* ATYP: доменное имя */
    req[4] = (uint8_t)hlen;
    memcpy(req + 5, host, hlen);
    req[5 + hlen] = (uint8_t)(port >> 8);
    req[6 + hlen] = (uint8_t)(port & 0xFF);

    const bool ok = (_tcp.write(req, reqlen) == reqlen);
    free(req);
    if (!ok) { _tcp.stop(); return 0; }

    /* Ответ: VER REP RSV ATYP ADDR PORT */
    uint8_t head[4];
    if (!readFully(head, 4) || head[0] != 0x05 || head[1] != 0x00) {
        _tcp.stop();
        return 0;
    }

    /* Пропускаем адрес/порт назначения из ответа */
    uint8_t tail[260];   /* ответ сервера: ATYP=домен => имя до 255 байт + порт */
    switch (head[3]) {
        case 0x01:                        /* IPv4 */
            if (!readFully(tail, 6)) { _tcp.stop(); return 0; }
            break;
        case 0x03: {                      /* доменное имя */
            uint8_t alen;
            if (!readFully(&alen, 1) || !readFully(tail, (size_t)alen + 2)) {
                _tcp.stop();
                return 0;
            }
            break;
        }
        case 0x04:                        /* IPv6 */
            if (!readFully(tail, 18)) { _tcp.stop(); return 0; }
            break;
        default:
            _tcp.stop();
            return 0;
    }

    return 1;
}

int Socks5Client::connect(IPAddress ip, uint16_t port) {
    if (!_tcp.connect(_proxyHost.c_str(), _proxyPort)) return 0;

    const uint8_t greet[3] = { 0x05, 0x01, 0x00 };
    if (_tcp.write(greet, 3) != 3) { _tcp.stop(); return 0; }

    uint8_t reply[2];
    if (!readFully(reply, 2) || reply[0] != 0x05 || reply[1] != 0x00) {
        _tcp.stop();
        return 0;
    }

    /* CONNECT, ATYP=0x01 — IPv4 */
    const uint8_t req[10] = {
        0x05, 0x01, 0x00, 0x01,
        ip[0], ip[1], ip[2], ip[3],
        (uint8_t)(port >> 8), (uint8_t)(port & 0xFF)
    };
    if (_tcp.write(req, 10) != 10) { _tcp.stop(); return 0; }

    uint8_t head[4];
    if (!readFully(head, 4) || head[0] != 0x05 || head[1] != 0x00) {
        _tcp.stop();
        return 0;
    }

    uint8_t tail[260];   /* ответ сервера: ATYP=домен => имя до 255 байт + порт */
    switch (head[3]) {
        case 0x01:
            if (!readFully(tail, 6)) { _tcp.stop(); return 0; }
            break;
        case 0x03: {
            uint8_t alen;
            if (!readFully(&alen, 1) || !readFully(tail, (size_t)alen + 2)) {
                _tcp.stop();
                return 0;
            }
            break;
        }
        case 0x04:
            if (!readFully(tail, 18)) { _tcp.stop(); return 0; }
            break;
        default:
            _tcp.stop();
            return 0;
    }

    return 1;
}

bool Socks5Client::readFully(uint8_t *buf, size_t len) {
    size_t got = 0;
    const uint32_t start = millis();
    while (got < len) {
        const int avail = _tcp.available();
        if (avail > 0) {
            const int n = _tcp.read(buf + got, len - got);
            if (n <= 0) return false;
            got += (size_t)n;
        } else if (millis() - start > _timeoutMs) {
            return false;
        } else {
            delay(1);
        }
    }
    return true;
}

size_t Socks5Client::write(uint8_t b) {
    return write(&b, 1);
}

size_t Socks5Client::write(const uint8_t *buf, size_t size) {
    return _tcp.write(buf, size);
}

int Socks5Client::available() {
    return _tcp.available();
}

int Socks5Client::read() {
    return _tcp.read();
}

int Socks5Client::read(uint8_t *buf, size_t size) {
    return _tcp.read(buf, size);
}

int Socks5Client::peek() {
    return _tcp.peek();
}

void Socks5Client::flush() {
    _tcp.flush();
}

void Socks5Client::stop() {
    _tcp.stop();
}

uint8_t Socks5Client::connected() {
    return _tcp.connected();
}
