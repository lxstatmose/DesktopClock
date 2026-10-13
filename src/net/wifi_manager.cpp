#include "wifi_manager.h"
#include "app_config.h"

#include <Arduino.h>
#include <WiFi.h>

/* Самовосстановление Wi-Fi без гонок с авто-reconnect фреймворка.
 *
 * В arduino-esp32 2.0.17 при событии DISCONNECTED стек САМ делает
 * disconnect()+begin() (см. WiFiGeneric.cpp: first_connect + авто-reconnect
 * для лечимых причин). Раньше наш loop каждые 15 с безусловно делал свой
 * disconnect — это убивало попытку стека прямо в полёте (в т.ч. во время
 * DHCP), и подключение могло не состояться. Новая схема:
 *   1) свежие события стека (< 20 с)  — стек сам чинится, не мешаем;
 *   2) после ассоциации ждём DHCP до 12 с;
 *   3) стек молчит (сдался/завис)     — принудительный begin раз в 15 с;
 *   4) офлайн >= 2 мин                — полный перезапуск стека раз в 30 с
 *      (независимо от событий — вылечивает любой «спин» авто-reconnect). */
#define WIFI_EVT_GRACE_MS   20000UL
#define WIFI_DHCP_WAIT_MS   12000UL
#define WIFI_KICK_MS        15000UL
#define WIFI_RESTART_MS     120000UL
#define WIFI_RESTART_PERIOD 30000UL

static uint32_t s_lastKick   = 0;   /* последний принудительный begin */
static uint32_t s_lastEvt    = 0;   /* последнее событие стека; 0 = не было */
static uint32_t s_lastAssoc  = 0;   /* последняя ассоциация с AP */
static uint32_t s_downSince  = 0;   /* когда пропала связь; 0 = онлайн */
static int      s_prevStatus = -1;  /* для лога переходов статуса */

static const char *statusName(int st) {
    switch (st) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_AP";
        case WL_SCAN_COMPLETED:  return "SCAN_DONE";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONN_FAILED";
        case WL_CONNECTION_LOST: return "CONN_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        case WL_NO_SHIELD:       return "NO_SHIELD";
        default:                 return "?";
    }
}

/* Общие настройки STA-интерфейса — используются при старте и при
 * полном перезапуске стека. */
static void sta_setup() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    /* WIFI_POWER_SAVE=1 → modem sleep: радио спит между DTIM-маяками.
     * Это НЕ light sleep — TCP/TLS соединения не рвутся, но нагрев и
     * средний ток заметно ниже. 0 — как раньше, радио активно всегда. */
#if WIFI_POWER_SAVE
    WiFi.setSleep(true);
#else
    WiFi.setSleep(false);        /* не засыпать — важно для TLS/бота */
#endif
    WiFi.setAutoReconnect(true);

    /* Явные DNS (если заданы) — до begin(), IP/GW остаются по DHCP */
    IPAddress dns1, dns2;
    dns1.fromString(WIFI_DNS1);
    dns2.fromString(WIFI_DNS2);
    if (dns1 != INADDR_NONE || dns2 != INADDR_NONE) {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns1, dns2);
        Serial.printf("[wifi] DNS: %s, %s\n", WIFI_DNS1, WIFI_DNS2);
    }
}

/* Диагностика: событие стека с причиной обрыва (reason-коды wifi_err_reason_t).
 * Имя причины (NO_AP_FOUND, AUTH_FAIL, BEACON_TIMEOUT...) сразу говорит,
 * кто виноват: роутер, пароль или канал. */
static void wifi_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        s_lastEvt = s_lastAssoc = millis();
        Serial.println("[wifi] событие: ASSOC");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        s_lastEvt = millis();
        Serial.printf("[wifi] событие: GOT_IP %s\n",
                      WiFi.localIP().toString().c_str());
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        s_lastEvt = millis();
        const uint8_t r = info.wifi_sta_disconnected.reason;
        Serial.printf("[wifi] событие: DISCONNECTED reason=%u (%s)\n",
                      (unsigned)r, WiFi.disconnectReasonName((wifi_err_reason_t)r));
        break;
    }
    default:
        break;
    }
}

void wifi_begin() {
    WiFi.onEvent(wifi_event);
    sta_setup();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_lastKick  = millis();
    s_lastEvt   = 0;   /* события стека ещё не приходили */
    s_lastAssoc = 0;
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_loop() {
    const int st = (int)WiFi.status();
    if (st != s_prevStatus) {
        Serial.printf("[wifi] статус: %s -> %s\n",
                      statusName(s_prevStatus), statusName(st));
        s_prevStatus = st;
    }

    if (st == WL_CONNECTED) {
        s_downSince = 0;
        return;
    }
    if (s_downSince == 0) s_downSince = millis() ? millis() : 1;

    const uint32_t now    = millis();
    const uint32_t downMs = now - s_downSince;

    /* 1) Офлайн >= 2 мин — полный перезапуск стека каждые 30 с.
     *    Независимо от событий: так выбивается и «зависший» драйвер, и
     *    бесконечный спин авто-reconnect, который сам не находит точку. */
    if (downMs >= WIFI_RESTART_MS) {
        if (now - s_lastKick < WIFI_RESTART_PERIOD) return;
        s_lastKick = now;
        Serial.printf("[wifi] офлайн %lu с — полный перезапуск стека (статус %s)\n",
                      (unsigned long)(downMs / 1000), statusName(st));
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        delay(250);
        sta_setup();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        return;
    }

    /* 2) Ассоциация была, но IP не приходит дольше 12 с — DHCP завис.
     *    Проверяем ДО правила «свежих событий»: во время зависшего DHCP
     *    события не приходят, и грейс-окно лишь тормозило бы пин. */
    if (st == WL_IDLE_STATUS && s_lastAssoc &&
        (now - s_lastAssoc) >= WIFI_DHCP_WAIT_MS &&
        (now - s_lastKick) >= WIFI_KICK_MS) {
        s_lastKick = now;
        Serial.printf("[wifi] DHCP завис после ассоциации (%lu с без IP) — переподключаем\n",
                      (unsigned long)((now - s_lastAssoc) / 1000));
        WiFi.disconnect(false, false);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        return;
    }

    /* 3) Стек сам ретраит (свежие события) — не вмешиваемся своим
     *    disconnect, иначе убьём его попытку в полёте. */
    if (s_lastEvt && (now - s_lastEvt) < WIFI_EVT_GRACE_MS) return;

    /* 4) Стек молчит (сдался или завис) — пинаем begin каждые 15 с. */
    if (now - s_lastKick < WIFI_KICK_MS) return;
    s_lastKick = now;

    Serial.printf("[wifi] тихий стек — принудительный begin (статус %s, офлайн %lu с)\n",
                  statusName(st), (unsigned long)(downMs / 1000));
    WiFi.disconnect(false, false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

/* Печать видимых сетей. Вызывается редко; scanNetworks нельзя выполнять,
 * пока идёт подключение — сначала полностью гасим STA-интерфейс. */
void wifi_log_scan() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(300);

    const int n = WiFi.scanNetworks(false, true);
    Serial.printf("[wifi] найдено сетей: %d\n", n);
    if (n > 0) {
        for (int i = 0; i < n && i < 20; i++) {
            Serial.printf("   %d) %s  (%d dBm)\n", i,
                          WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i));
        }
    } else if (n < 0) {
        Serial.println("[wifi] ошибка сканирования");
    }
    WiFi.scanDelete();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_lastKick = millis();
    s_lastEvt  = millis();   /* сами начали попытку — дать стеку шанс */
}
