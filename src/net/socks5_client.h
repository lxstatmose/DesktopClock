#pragma once
/*
 * socks5_client.h — Arduino Client поверх SOCKS5-прокси.
 *
 * Позволяет «сырому» TCP-соединению пройти через SOCKS5 (роутер 192.168.2.1:1080).
 * DNS разрешает прокси (SOCKS5h, ATYP=0x03) — важно, когда локальный
 * DNS перехватывается роутером/провайдером.
 */
#include <Arduino.h>
#include <Client.h>
#include <WiFiClient.h>

class Socks5Client : public Client {
public:
    void  setProxy(const char *host, uint16_t port);
    void  setTimeout(uint32_t ms);

    int   connect(IPAddress ip, uint16_t port) override;
    int   connect(const char *host, uint16_t port) override;
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buf, size_t size) override;
    int   available() override;
    int   read() override;
    int   read(uint8_t *buf, size_t size) override;
    int   peek() override;
    void  flush() override;
    void  stop() override;
    uint8_t connected() override;
    operator bool() override { return connected() != 0; }

private:
    bool readFully(uint8_t *buf, size_t len);

    WiFiClient _tcp;
    String     _proxyHost;
    uint16_t   _proxyPort = 0;
    uint32_t   _timeoutMs = 15000;
};

/*
 * socks5_open_tunnel() — тот же SOCKS5 CONNECT, но возвращает «сырой»
 * файловый дескриптор вместо обёртки Client. Нужен, когда поверх туннеля
 * строится TLS: WiFiClientSecure в arduino-esp32 2.0.17 всегда создаёт СВОЙ
 * сокет и подключается напрямую (start_ssl_client), поэтому TLS через
 * прокси приходится делать самому (mbedTLS поверх этого fd).
 *
 * Владение сокетом переходит к вызывающему: закрывать его нужно вручную
 * (close(fd)). Возвращает -1 при любой ошибке.
 * ATYP=0x03 — имя хоста резолвит сам прокси (SOCKS5h).
 */
int socks5_open_tunnel(const char *proxyHost, uint16_t proxyPort,
                       const char *host, uint16_t port, uint32_t timeoutMs);
