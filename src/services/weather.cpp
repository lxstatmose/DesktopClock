#include "weather.h"

#include "app_config.h"
#include "app_state.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <stdlib.h>

#include "net/socks5_client.h"

/* Погода: OpenWeatherMap (Current Weather API, HTTPS/JSON, API-ключ
 * из app_config.h). Только через SOCKS5-прокси роутера.
 * Прямой доступ в интернет блокируется сетью. */

constexpr char WEATHER_API_HOST[] = "api.openweathermap.org";
constexpr char WEATHER_API_PATH[] = "/data/2.5/weather";
/* ВАЖНО: запрос собирается ОДНИМ snprintf, и спецификаторы %.4f/%s стоят
 * прямо в формате (см. weather_fetch). Раньше параметры подставлялись вторым
 * "%s" из строки "lat=%.4f&lon=%.4f&appid=%s" — и уходили в запрос БУКВАЛЬНО
 * (snprintf не раскрывает % внутри подставленного аргумента). OWM получал
 * appid=%s и отвечал 401 «Invalid API key». */
constexpr uint16_t WEATHER_API_PORT = 80;
constexpr uint32_t WEATHER_HTTP_TIMEOUT_MS = 20000;
constexpr uint32_t WEATHER_SOCKS_TIMEOUT_MS = 15000;

/* Пропустить заголовки HTTP-ответа и вернуть true при успехе. */
static bool fetch_weather_json(const char *path,
                                JsonDocument &doc, const JsonDocument &filter) {
    const uint32_t t3 = millis();
    Socks5Client socks;
    socks.setProxy(SOCKS5_HOST, SOCKS5_PORT);
    socks.setTimeout(WEATHER_SOCKS_TIMEOUT_MS);

    /* SOCKS5 CONNECT к целевому хосту */
    if (!socks.connect(WEATHER_API_HOST, WEATHER_API_PORT)) {
        Serial.println("[weather] SOCKS5 connect() не удался");
        return false;
    }

    /* Минимальный HTTP/1.1 GET-запрос */
    socks.printf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.5.0\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                 path, WEATHER_API_HOST);

    /* Пропустить заголовки HTTP-ответа. Между пакетами спим, иначе цикл
     * занимает 100% ядра до 20 с и греет чип впустую. */
    bool headersDone = false;
    uint32_t dl = millis();
    int httpStatus = -1;
    while (millis() - dl < 20000) {
        if (socks.available()) {
            String line = socks.readStringUntil('\n');
            if (httpStatus < 0 && line.startsWith("HTTP/")) {
                /* Парсим статус: "HTTP/1.1 200 OK" */
                int sp1 = line.indexOf(' ');
                int sp2 = line.indexOf(' ', sp1 + 1);
                if (sp1 >= 0 && sp2 > sp1) {
                    httpStatus = line.substring(sp1 + 1, sp2).toInt();
                }
            }
            if (line == "\r" || line.length() == 0) {
                headersDone = true;
                break;
            }
        } else {
            delay(5);
        }
    }

    if (!headersDone) {
        Serial.println("[weather] SOCKS5 timeout ожидания заголовков");
        socks.stop();
        return false;
    }

    if (httpStatus < 200 || httpStatus >= 300) {
        /* 401/404 приходят уже от OWM — значит SOCKS5-туннель работает,
         * а запрос отклонён на стороне API (ключ/подписка). */
        if (httpStatus == 401)
            Serial.println("[weather] HTTP 401 — OWM отклонил API-ключ "
                           "(неверный, удалённый или ещё не активирован)");
        else if (httpStatus == 404)
            Serial.println("[weather] HTTP 404 — неверный путь или хост");
        else if (httpStatus < 0)
            Serial.println("[weather] нет HTTP-статуса в ответе");
        else
            Serial.printf("[weather] HTTP error: %d\n", httpStatus);
        socks.stop();
        return false;
    }

    /* Читаем тело и парсим JSON с фильтром */
    const DeserializationError err =
        deserializeJson(doc, socks, DeserializationOption::Filter(filter));
    socks.stop();
    if (!err) {
        Serial.printf("[weather] SOCKS5 %s ok за %u мс\n", WEATHER_API_HOST,
                      (unsigned)(millis() - t3));
        return true;
    }
    Serial.printf("[weather] SOCKS5 json error: %s\n", err.c_str());
    return false;
}

/* OWM weather.code -> WMO код */
static int owm_to_wmo(int c) {
    if (c == 800) return 0;
    if (c == 801) return 1;
    if (c == 802) return 2;
    if (c == 803 || c == 804) return 3;
    if (c == 701 || c == 721 || c == 741) return 45;
    if (c >= 731 && c <= 762) return 45;
    if (c == 781) return 95;
    if (c >= 300 && c <= 302) return 51;
    if (c >= 310 && c <= 311) return 53;
    if (c == 312 || c == 313) return 56;
    if (c == 314) return 57;
    if (c == 321) return 55;
    if (c == 500) return 61;
    if (c == 501) return 63;
    if (c >= 502 && c <= 504) return 65;
    if (c == 511) return 66;
    if (c >= 520 && c <= 521) return 80;
    if (c == 522) return 81;
    if (c == 531) return 82;
    if (c == 600) return 71;
    if (c == 601) return 73;
    if (c == 602) return 75;
    if (c == 611 || c == 612 || c == 613) return 67;
    if (c == 615 || c == 616) return 66;
    if (c == 620) return 71;
    if (c >= 621 && c <= 622) return 73;
    if (c >= 200 && c <= 232) {
        if (c == 212 || c == 221 || c == 232) return 99;
        return 95;
    }
    return 2;
}

static float jnum(JsonVariantConst v, float def) {
    if (v.is<float>() || v.is<int>()) return v.as<float>();
    const char *s = v.as<const char *>();
    return (s && *s) ? (float)atof(s) : def;
}

static int unix_to_local_minutes(long utc, long tzOffset) {
    time_t t = (time_t)utc + tzOffset;
    struct tm lt;
    gmtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

static int now_minutes_local() {
    const time_t t = time(nullptr);
    if (t < 1000000000L) return -1;
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

bool weather_update() {
    if (WiFi.status() != WL_CONNECTED) return false;

    char path[160];
    /* Формат должен содержать спецификаторы %.4f/%s напрямую: если подставлять
     * строку с '%.4f' через %s, snprintf вставит её буквально (см. комментарий
     * у WEATHER_API_QUERY). Поэтому путь/координаты/ключ — аргументами здесь. */
    snprintf(path, sizeof(path),
             "%s?lat=%.4f&lon=%.4f&appid=%s&units=metric&lang=ru",
             WEATHER_API_PATH, (double)WEATHER_LAT, (double)WEATHER_LON,
             WEATHER_API_KEY);

#if ARDUINOJSON_VERSION_MAJOR > 6
    JsonDocument doc;
    JsonDocument filter;
#else
    DynamicJsonDocument doc(4096);
    DynamicJsonDocument filter(512);
#endif
    filter["weather"][0]["id"]    = true;
    filter["main"]["temp"]        = true;
    filter["main"]["feels_like"]  = true;
    filter["main"]["humidity"]    = true;
    filter["main"]["pressure"]    = true;
    filter["wind"]["speed"]       = true;
    filter["wind"]["deg"]         = true;
    filter["sys"]["sunrise"]      = true;
    filter["sys"]["sunset"]       = true;
    filter["timezone"]            = true;

    if (!fetch_weather_json(path, doc, filter)) {
        Serial.println("[weather] OpenWeatherMap недоступен или ошибка парсинга");
        return false;
    }

    JsonVariantConst cur = doc["main"];
    JsonArrayConst warr = doc["weather"].as<JsonArrayConst>();
    if (cur.isNull() || warr.isNull() || warr.size() == 0) {
        Serial.println("[weather] нет main/weather в ответе");
        return false;
    }

    WeatherData w;
    w.valid       = true;
    w.tempC       = jnum(cur["temp"], 0);
    w.feelsC      = jnum(cur["feels_like"], w.tempC);
    w.humidity    = (int)jnum(cur["humidity"], 0);
    w.pressureHpa = jnum(cur["pressure"], 0);
    w.windMs      = jnum(doc["wind"]["speed"], 0);
    w.windDir     = (int)jnum(doc["wind"]["deg"], 0);
    w.code        = owm_to_wmo((int)jnum(warr[0]["id"], -1));
    w.updatedMs   = millis();

    long tzOff = doc["timezone"].as<long>();
    JsonVariantConst sr = doc["sys"]["sunrise"];
    JsonVariantConst ss = doc["sys"]["sunset"];
    w.sunriseMin = sr.isNull() ? -1 : unix_to_local_minutes(sr.as<long>(), tzOff);
    w.sunsetMin  = ss.isNull() ? -1 : unix_to_local_minutes(ss.as<long>(), tzOff);

    const int nowMin = now_minutes_local();
    w.isDay = 1;
    if (nowMin >= 0 && w.sunriseMin >= 0 && w.sunsetMin >= 0)
        w.isDay = (nowMin >= w.sunriseMin && nowMin < w.sunsetMin) ? 1 : 0;

    state_lock();
    g_state.weather = w;
    state_unlock();

    Serial.printf("[weather] %.1f C, %s (восход %d, закат %d)\n",
                  (double)w.tempC, wmo_text(w.code), w.sunriseMin, w.sunsetMin);
    return true;
}

/* WMO weather codes */
const char *wmo_text(int code) {
    switch (code) {
        case 0:            return "Ясно";
        case 1:            return "Преимущественно ясно";
        case 2:            return "Переменная облачность";
        case 3:            return "Пасмурно";
        case 45: case 48:  return "Туман";
        case 51: case 53:
        case 55:           return "Морось";
        case 56: case 57:  return "Ледяная морось";
        case 61:           return "Небольшой дождь";
        case 63:           return "Дождь";
        case 65:           return "Сильный дождь";
        case 66: case 67:  return "Ледяной дождь";
        case 71:           return "Небольшой снег";
        case 73:           return "Снег";
        case 75:           return "Сильный снег";
        case 77:           return "Снежные зёрна";
        case 80: case 81:
        case 82:           return "Ливни";
        case 85: case 86:  return "Снежные ливни";
        case 95:           return "Гроза";
        case 96: case 99:  return "Гроза с градом";
        default:           return "—";
    }
}

const char *wmo_short(int code) {
    if (code == 0 || code == 1)  return "Ясно";
    if (code == 2)               return "Перемен. облачн.";
    if (code == 3)               return "Пасмурно";
    if (code == 45 || code == 48) return "Туман";
    if (code >= 51 && code <= 57) return "Морось";
    if (code >= 61 && code <= 67) return "Дождь";
    if (code >= 71 && code <= 77) return "Снег";
    if (code >= 80 && code <= 82) return "Ливни";
    if (code == 85 || code == 86) return "Снегопад";
    if (code >= 95)              return "Гроза";
    return "—";
}