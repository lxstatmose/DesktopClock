#include "net_task.h"

#include "app_config.h"
#include "app_state.h"

#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "sensors/ens160_aht21.h"
#include "services/weather.h"
#include "services/notify.h"
#include "services/metrics.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

static uint32_t s_lastSensor   = 0;
static uint32_t s_lastWeather  = 0;
static uint32_t s_lastSys      = 0;
static bool     s_weatherFirst = true;

static int      s_lastDay   = -1;
static bool     s_todaySent = false;
static uint32_t s_nextTry   = 0;

static void update_sys_info() {
    state_lock();
    SystemInfo &s = g_state.sys;

    s.wifiOnline = (WiFi.status() == WL_CONNECTED);
    if (s.wifiOnline) {
        const String ip = WiFi.localIP().toString();
        snprintf(s.ip, sizeof(s.ip), "%s", ip.c_str());
        s.rssi = (int8_t)WiFi.RSSI();
    } else {
        s.ip[0] = '\0';
        s.rssi  = 0;
    }
    s.uptimeSec = millis() / 1000UL;
    s.freeHeap  = (uint32_t)ESP.getFreeHeap();
    s.chipTempC = temperatureRead();   /* встроенный датчик температуры ядра */

    state_unlock();
}

static void read_sensors() {
    HomeData h;
    memset(&h, 0, sizeof(h));
    if (sensor_read(h)) {
        state_lock();
        g_state.home = h;
        state_unlock();
    }
}

static void net_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));

    sensor_begin();
    time_begin();

    for (;;) {
        wifi_loop();

        /* диагностика раз в 5 с — чтобы видеть, что блокирует */
        static uint32_t s_lastDiag = 0;
        if (millis() - s_lastDiag > 5000) {
            s_lastDiag = millis();
            time_t now = time(nullptr);
            Serial.printf("[net] wifi=%d time=%ld valid=%d heap=%u\n",
                          (int)WiFi.status(), (long)now, (int)time_is_valid(),
                          (unsigned)ESP.getFreeHeap());
        }

        if (wifi_is_connected()) {
            /* Уведомления на телефон: проверка аварийных порогов */
            notify_check_alerts();

            /* Ежедневная сводка погоды в заданный час (UTC+3) */
            if (time_is_valid()) {
                const time_t now = time(nullptr);
                struct tm lt;
                localtime_r(&now, &lt);

                if (lt.tm_mday != s_lastDay) {     /* смена суток — сбрасываем «сегодня» */
                    s_lastDay   = lt.tm_mday;
                    s_todaySent = false;
                }
                if (!s_todaySent && lt.tm_hour >= NOTIFY_DAILY_HOUR && millis() > s_nextTry) {
                    /* Снимок погоды ПОД мьютексом, отправка — БЕЗ него:
                     * notify_send() ходит в сеть (до 20 с), удержание
                     * g_stateMutex блокировало бы UI-поток на всё это время. */
                    char weatherText[400];
                    bool haveData;
                    {
                        StateGuard g;
                        haveData = g_state.weather.valid;
                        const WeatherData &w = g_state.weather;
                        if (!haveData) {
                            snprintf(weatherText, sizeof(weatherText), "Погода в %s\nданных пока нет", WEATHER_CITY_NAME);
                        } else {
                            snprintf(weatherText, sizeof(weatherText),
                                     "Погода в %s\n%+.1f°C, ощущается %+.1f°C\n%s\n"
                                     "Влажность %d%%, ветер %.1f м/с\nДавление %.0f мм рт.ст.",
                                     WEATHER_CITY_NAME, (double)w.tempC, (double)w.feelsC,
                                     wmo_short(w.code), w.humidity, (double)w.windMs,
                                     (double)(w.pressureHpa * 0.750062f));
                        }
                    }
                    if (notify_send_weather(weatherText)) {
                        s_todaySent = true;
                    } else {
                        s_nextTry = millis() + (5UL * 60UL * 1000UL);   /* повтор через 5 мин */
                    }
                }
            }

            /* Погода не требует синхронизации времени — загружаем сразу при наличии Wi-Fi.
             * Интервал выбирается по факту наличия данных: пока погода
             * не приехала, повторяем часто (WEATHER_RETRY_MS), иначе
             * ждём полный WEATHER_UPDATE_MS. */
            state_lock();
            const bool gotData = g_state.weather.valid;
            state_unlock();
            if (s_weatherFirst ||
                (millis() - s_lastWeather >= (gotData ? WEATHER_UPDATE_MS
                                                      : WEATHER_RETRY_MS))) {
                s_weatherFirst = false;
                s_lastWeather  = millis();
                weather_update();
            }

            /* Хаб домашнего сервера: метрики сервера/ПК + уведомления бота.
             * Обычный HTTP в локалке, поэтому быстро и без TLS. */
            static uint32_t lastHub = 0;
            if (METRICS_HUB_ENABLED && (millis() - lastHub >= METRICS_POLL_MS)) {
                lastHub = millis();
                metrics_fetch();
            }
        }

        if (millis() - s_lastSensor >= SENSOR_UPDATE_MS) {
            s_lastSensor = millis();
            read_sensors();
        }

        if (millis() - s_lastSys >= 1000) {
            s_lastSys = millis();
            update_sys_info();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void net_task_start() {
    /* 24 КБ стека: TLS-рукопожатия (HTTPS погоды и ntfy через SOCKS5)
     * плюс буферы HTTP-обмена; 16 КБ было ограничением. */
    xTaskCreatePinnedToCore(net_task, "net", 24576, nullptr, 1, nullptr, 0);
}