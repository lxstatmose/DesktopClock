#include "metrics.h"

#include "app_config.h"
#include "app_state.h"
#include "net/http_tunnel.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Плата поддерживает два источника (пробуются по порядку в metrics_fetch):
 *
 * 1) GET /metrics — «богатый» формат:
 *    {"server":{"cpu_percent":0.0,"cpu_temp":"36.0°C","ram_used":3.7,
 *               "ram_total":15.4,"disk_temp":"NVMe: 54°C (макс), Плата: 27°C",
 *               "uptime":"1d 23h 23m"},
 *     "pc":{"gpu_percent":12,"disk_temp":57,"ram_total":31.7,"cpu_percent":24.4,
 *           "device_name":"RTX 4060","ram_used":15.7,"cpu_temp":45,"gpu_temp":46}}
 *
 * 2) GET /api/state — формат server/clock-hub.py (RAM в БАЙТАХ, uptime в
 *    СЕКУНДАХ, температура в поле temp, при протухании — "stale": true).
 *
 * Имена устройств на экранах заданы статически («Сервер»/«ПК») и из
 * метрик не берутся. */

static float parse_temp(const char *s) {
    if (!s || !*s) return -999.0f;
    /* "36.0°C" или "45" */
    return (float)atof(s);
}

static float parse_temp_num(JsonVariantConst v) {
    if (v.is<int>()) return (float)v.as<int>();
    if (v.is<float>()) return v.as<float>();
    const char *s = v.as<const char *>();
    return parse_temp(s);
}

/* Поле-число с fallback на альтернативное имя (cpu_percent -> cpu). */
static float field_num(JsonVariantConst v, const char *a, const char *b, float def) {
    JsonVariantConst x = v[a];
    if (x.isNull() && b) x = v[b];
    if (x.isNull()) return def;
    return parse_temp_num(x);
}

static uint32_t parse_uptime(const char *s) {
    if (!s || !*s) return 0;
    int days = 0, hours = 0, mins = 0;
    /* "1d 23h 23m" или "1d 5h" или "5h 30m" */
    sscanf(s, "%dd %dh %dm", &days, &hours, &mins);
    if (days == 0 && hours == 0) {
        sscanf(s, "%dh %dm", &hours, &mins);
    }
    return (uint32_t)(days * 86400 + hours * 3600 + mins * 60);
}

/* Aптайм: строкой ("1d 23h") либо секундами числом (clock-hub). */
static uint32_t parse_uptime_any(JsonVariantConst v) {
    if (v.isNull()) return 0;
    if (v.is<const char *>()) return parse_uptime(v.as<const char *>());
    return (uint32_t)v.as<double>();
}

/* Извлекаем первую температуру из строки "NVMe: 54°C (макс), Плата: 27°C" */
static float parse_disk_temp_str(const char *s) {
    if (!s || !*s) return -999.0f;
    const char *p = strchr(s, ':');
    if (!p) return -999.0f;
    p++;
    while (*p == ' ') p++;
    return parse_temp(p);
}

/* Температура диска: строка с двоеточиями, простое число или отсутствует. */
static float parse_disk_temp(JsonVariantConst v) {
    if (v.isNull()) return -999.0f;
    const char *s = v.as<const char *>();
    if (s && *s && strchr(s, ':')) return parse_disk_temp_str(s);
    return parse_temp_num(v);
}

/* RAM: ГБ (ram_used, /metrics) либо байты (mem_used, clock-hub). */
static void parse_ram(JsonVariantConst v, DeviceMetrics &d) {
    JsonVariantConst u = v["ram_used"];
    if (!u.isNull()) {
        d.memUsedMb  = (uint32_t)(u.as<double>() * 1024.0);
        d.memTotalMb = (uint32_t)(v["ram_total"].as<double>() * 1024.0);
        return;
    }
    u = v["mem_used"];
    d.memUsedMb  = (uint32_t)(u.as<double>() / 1048576.0);
    d.memTotalMb = (uint32_t)(v["mem_total"].as<double>() / 1048576.0);
}

/* Протухший снимок (clock-hub: агент молчит > STALE_SEC) считаем
 * отсутствием данных — экран покажет «нет данных». */
static bool is_stale(JsonVariantConst v) {
    return v["stale"] | false;
}

static bool parse_server(JsonVariantConst v, DeviceMetrics &d) {
    if (v.isNull() || is_stale(v)) return false;

    snprintf(d.name, sizeof(d.name), "%s", "Сервер");
    d.cpuPct = field_num(v, "cpu_percent", "cpu", 0.0f);
    d.gpuPct = 0.0f;

    /* Температура CPU: "36.0°C" (/metrics) либо temp — число (clock-hub). */
    JsonVariantConst t = v["cpu_temp"];
    d.tempC = t.isNull() ? field_num(v, "temp", nullptr, -999.0f)
                         : parse_temp_num(t);

    parse_ram(v, d);

    d.diskTempC = parse_disk_temp(v["disk_temp"]);
    d.uptimeSec = parse_uptime_any(v["uptime"]);
    d.gpuTempC  = -999.0f;
    d.load1     = v["load1"] | -1.0f;
    d.updatedMs = millis();
    d.valid     = true;
    return true;
}

static bool parse_pc(JsonVariantConst v, DeviceMetrics &d) {
    if (v.isNull() || is_stale(v)) return false;

    snprintf(d.name, sizeof(d.name), "%s", "ПК");
    d.cpuPct = field_num(v, "cpu_percent", "cpu", 0.0f);
    d.gpuPct = field_num(v, "gpu_percent", "gpu", 0.0f);

    JsonVariantConst ct = v["cpu_temp"];
    d.tempC = ct.isNull() ? field_num(v, "temp", nullptr, -999.0f)
                          : parse_temp_num(ct);

    /* У clock-hub нет температуры GPU — остаётся -999, экран покажет «—» */
    d.gpuTempC = v["gpu_temp"].isNull() ? -999.0f
                                        : parse_temp_num(v["gpu_temp"]);

    parse_ram(v, d);

    d.diskTempC = parse_disk_temp(v["disk_temp"]);
    d.uptimeSec = parse_uptime_any(v["uptime"]);
    d.load1     = v["load1"] | -1.0f;
    d.updatedMs = millis();
    d.valid     = true;
    return true;
}

bool metrics_fetch(void) {
#if !METRICS_HUB_ENABLED
    return false;
#else
    char buf[1536];
    const char *tok = METRICS_HUB_TOKEN[0] ? METRICS_HUB_TOKEN : nullptr;

    /* Сначала /metrics, при неудаче (404 и т.п.) — /api/state (clock-hub). */
    if (!http_get_plain(METRICS_HUB_HOST, METRICS_HUB_PORT, "/metrics",
                        tok, 8000, buf, sizeof(buf)) &&
        !http_get_plain(METRICS_HUB_HOST, METRICS_HUB_PORT, "/api/state",
                        tok, 8000, buf, sizeof(buf))) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        Serial.println("[metrics] не разобрали JSON от хаба");
        return false;
    }

    DeviceMetrics srv, pc;
    memset(&srv, 0, sizeof(srv));
    memset(&pc, 0, sizeof(pc));

    const bool okSrv = parse_server(doc["server"], srv);
    const bool okPc  = parse_pc(doc["pc"], pc);

    state_lock();
    if (okSrv)  g_state.server = srv;
    if (okPc)   g_state.pc     = pc;
    state_unlock();

    /* Уведомление от бота: хаб кладёт последнее в поле "notice"
     * {id, text, ts}; id монотонно растёт — уже показанное не трогаем
     * (экран дополнительно фильтрует по id). Отсутствие поля — норма. */
    JsonVariantConst nv = doc["notice"];
    if (!nv.isNull()) {
        const uint32_t nid    = (uint32_t)(nv["id"] | 0);
        const char    *ntext  = nv["text"] | "";
        if (nid != 0 && ntext[0]) {
            state_lock();
            if (g_state.notice.id != nid) {
                g_state.notice.valid = true;
                g_state.notice.id    = nid;
                snprintf(g_state.notice.text, sizeof(g_state.notice.text),
                         "%s", ntext);
                g_state.notice.createdMs = millis();
            }
            state_unlock();
        }
    }

    return okSrv || okPc;
#endif
}