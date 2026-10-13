#pragma once
/*
 * http_tunnel.h — HTTP-клиент для этой сети.
 *
 * На этой сети прямой TCP в интернет с ESP32 блокируется, а SOCKS5-прокси
 * роутера режет часть доменов по SNI (имени в TLS-рукопожатии). При этом
 * WiFiClientSecure в arduino-esp32 2.0.17 не умеет работать через прокси:
 * start_ssl_client() всегда создаёт свой сокет и подключается напрямую.
 * Поэтому здесь TLS собран вручную на mbedTLS поверх сокета SOCKS5-туннеля
 * (socks5_open_tunnel).
 *
 * Используется сервисами, которым нужны исходящие POST-запросы
 * (weather, уведомления).
 */
#include <Arduino.h>

/* POST через SOCKS5-туннель + TLS (основной путь на этой сети).
 * contentType может быть NULL. Возвращает true при успешном HTTP-ответе
 * (2xx), текст ответа (если не NULL и не пустой указатель) кладётся в body. */
bool http_post_via_socks5(const char *host, uint16_t port,
                          const char *path, const char *contentType,
                          const char *body, uint32_t timeoutMs,
                          char *resp, size_t respSize);

/* Обычный GET без TLS — для обращения к своему серверу в локальной сети
 * (там TLS и прокси не нужны: это прямой доступ по адресу 192.168.2.x).
 * token != nullptr -> добавляется заголовок X-Hub-Token (авторизация на хабе). */
bool http_get_plain(const char *host, uint16_t port, const char *path,
                    const char *token, uint32_t timeoutMs,
                    char *resp, size_t respSize);