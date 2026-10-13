#include "notify.h"

#include "app_config.h"
#include "app_state.h"
#include "net/http_tunnel.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string.h>
#include <stdio.h>
#include <time.h>

#if NOTIFY_NTFY_ENABLED

#define NTFY_HOST  "ntfy.sh"
#define NTFY_PORT  443

/* Топик ntfy — это фактически «пароль»: кто знает имя, тот может читать и
 * писать. Поэтому берём длинное случайное имя (см. NOTIFY_NTFY_TOPIC). */

/* ntfy принимает сообщение телом POST; заголовки Title/Priority необязательны.
 * Заголовки добавляем прямо в строку запроса, поэтому здесь нужен
 * собственный POST — общий транспорт шлёт только Content-Type. */
static bool ntfy_send(const char *title, const char *text) {
    char path[80];
    snprintf(path, sizeof(path), "/%s", NOTIFY_NTFY_TOPIC);

    char body[420];
    size_t o = 0;
    for (const char *s = text; *s && o < sizeof(body) - 6; s++) {
        const unsigned char c = (unsigned char)*s;
        /* ntfy: переводы строк допустимы, но убираем CR и прочие управляющие */
        if (c == '\r')              continue;
        if (c < 0x20 && c != '\n')  continue;
        body[o++] = (char)c;
    }
    body[o] = '\0';

    char resp[256];
    const bool ok = http_post_via_socks5(NTFY_HOST, NTFY_PORT, path,
                                         "text/plain; charset=utf-8", body,
                                         20000, resp, sizeof(resp));
    Serial.printf("[ntfy] %s -> %s\n", title ? title : "сообщение",
                  ok ? "доставлено" : "ошибка");
    return ok;
}

#endif /* NOTIFY_NTFY_ENABLED */

bool notify_enabled(void) {
    bool any = false;
#if NOTIFY_NTFY_ENABLED
    if (NOTIFY_NTFY_TOPIC[0] != '\0') any = true;
#endif
    return any;
}

bool notify_send(const char *text) {
    if (!text || !*text) return false;
    if (!notify_enabled()) return false;
    if (!WiFi.isConnected()) {
        Serial.println("[notify] нет Wi-Fi — сообщение пропущено");
        return false;
    }

    bool any = false;

#if NOTIFY_NTFY_ENABLED
    if (NOTIFY_NTFY_TOPIC[0] != '\0') {
        /* Первая строка текста идёт заголовком, остальное — телом */
        char title[64];
        const char *nl = strchr(text, '\n');
        size_t tl = nl ? (size_t)(nl - text) : strlen(text);
        if (tl > sizeof(title) - 1) tl = sizeof(title) - 1;
        memcpy(title, text, tl);
        title[tl] = '\0';

        const char *rest = nl ? nl + 1 : "";
        if (ntfy_send(title, *rest ? rest : text)) any = true;
    }
#endif

    if (!any) Serial.println("[notify] ни один канал не доставил сообщение");
    return any;
}

bool notify_send_weather(const char *weatherText) {
    if (!weatherText || !*weatherText) return false;
    return notify_send(weatherText);
}

/* ---------------- Проверка аварийных порогов ---------------- */

static bool     s_alertHomeSent   = false;
static bool     s_alertServerSent = false;
static bool     s_alertPcSent     = false;
static uint32_t s_lastAlertCheck  = 0;

#define ALERT_CHECK_INTERVAL_MS (60UL * 1000UL)   /* проверка раз в минуту */

void notify_check_alerts(void) {
    if (!notify_enabled()) return;
    if (!WiFi.isConnected()) return;

    const uint32_t now = millis();
    if (now - s_lastAlertCheck < ALERT_CHECK_INTERVAL_MS) return;
    s_lastAlertCheck = now;

    /* Снимок данных ПОД мьютексом, дальше — только локальные копии:
     * notify_send() ходит в сеть (до 20 с), и удержание g_stateMutex
     * на всё это время блокировало бы UI-поток (ядро 1) на секунды. */
    bool  homeValid, serverValid, pcValid;
    float homeT, srvCpu, srvT, pcCpu, pcT;
    {
        StateGuard g;
        homeValid   = g_state.home.valid;
        homeT       = g_state.home.tempC;
        serverValid = g_state.server.valid;
        srvCpu      = g_state.server.cpuPct;
        srvT        = g_state.server.tempC;
        pcValid     = g_state.pc.valid;
        pcCpu       = g_state.pc.cpuPct;
        pcT         = g_state.pc.tempC;
    }

    /* 1. Дом: высокая температура (AHT21) */
    if (homeValid && homeT >= ALERT_HOME_TEMP_C) {
        if (!s_alertHomeSent) {
            char b[160];
            snprintf(b, sizeof(b),
                     "🏠 ВНИМАНИЕ: высокая температура дома!\n%.1f°C (порог %.0f°C)",
                     (double)homeT, (double)ALERT_HOME_TEMP_C);
            if (notify_send(b)) s_alertHomeSent = true;
        }
    } else {
        s_alertHomeSent = false;   /* сброс при возврате в норму */
    }

    /* 2. Сервер: высокая загрузка CPU или температура */
    if (serverValid) {
        const bool serverAlert = (srvCpu >= ALERT_SERVER_CPU_PCT) ||
                                 (srvT >= ALERT_SERVER_TEMP_C);
        if (serverAlert && !s_alertServerSent) {
            char b[160];
            snprintf(b, sizeof(b),
                     "🖥️ ВНИМАНИЕ: сервер перегружен!\nCPU: %.0f%% (порог %.0f%%)  T: %.0f°C (порог %.0f°C)",
                     (double)srvCpu, (double)ALERT_SERVER_CPU_PCT,
                     (double)srvT, (double)ALERT_SERVER_TEMP_C);
            if (notify_send(b)) s_alertServerSent = true;
        } else if (!serverAlert) {
            s_alertServerSent = false;
        }
    }

    /* 3. ПК: высокая загрузка CPU или температура */
    if (pcValid) {
        const bool pcAlert = (pcCpu >= ALERT_PC_CPU_PCT) ||
                             (pcT >= ALERT_PC_TEMP_C);
        if (pcAlert && !s_alertPcSent) {
            char b[160];
            snprintf(b, sizeof(b),
                     "💻 ВНИМАНИЕ: ПК перегружен!\nCPU: %.0f%% (порог %.0f%%)  T: %.0f°C (порог %.0f°C)",
                     (double)pcCpu, (double)ALERT_PC_CPU_PCT,
                     (double)pcT, (double)ALERT_PC_TEMP_C);
            if (notify_send(b)) s_alertPcSent = true;
        } else if (!pcAlert) {
            s_alertPcSent = false;
        }
    }
}