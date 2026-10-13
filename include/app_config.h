#pragma once
/*
 * app_config.h — рабочие настройки устройства.
 * Секреты вынесены в app_config_secrets.h (исключён из git).
 */

#include "app_config_secrets.h"

/* ================= WiFi ================= */
/* ВАЖНО: iOS по умолчанию называет точку доступа типографским апострофом
 * U+2019 (’), а не прямым ' — из-за этого SSID не совпадает. */
#define WIFI_SSID           SECRET_WIFI_SSID
#define WIFI_PASSWORD       SECRET_WIFI_PASSWORD
#define WIFI_HOSTNAME       "desktop-clock"

/* ================= Нагрев / энергосбережение ================= */
#define WIFI_POWER_SAVE     1
#define CPU_FREQ_MHZ        160

/* ================= DNS ================= */
#define WIFI_DNS1           "8.8.8.8"
#define WIFI_DNS2           "8.8.4.4"

/* ================= SOCKS5 прокси (роутер) ================= */
#define WEATHER_USE_SOCKS5  1
#define SOCKS5_HOST         SECRET_SOCKS5_HOST
#define SOCKS5_PORT         SECRET_SOCKS5_PORT

/* ================= Время ================= */
#define TIMEZONE            "MSK-3"
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define NTP_SERVER_3        "time.cloudflare.com"

/* ================= Погода (OpenWeatherMap) ================= */
#define WEATHER_API_KEY     SECRET_WEATHER_API_KEY
#define WEATHER_LAT         55.7558f
#define WEATHER_LON         37.6173f
#define WEATHER_CITY_NAME   "Москва"
#define WEATHER_UPDATE_MS   (15UL * 60UL * 1000UL)
#define WEATHER_RETRY_MS    (60UL * 1000UL)

/* ================= Хаб метрик (домашний сервер) ================= */
#define METRICS_HUB_ENABLED  1
#define METRICS_HUB_HOST     SECRET_METRICS_HUB_HOST
#define METRICS_HUB_PORT     SECRET_METRICS_HUB_PORT
/* X-Hub-Token; пустая строка = без токена (хаб открыт). */
#define METRICS_HUB_TOKEN    ""
#define METRICS_POLL_MS      (10UL * 1000UL)

/* ================= Уведомления на телефон (ntfy.sh) ================= */
#define NOTIFY_NTFY_ENABLED  1
#define NOTIFY_NTFY_TOPIC    SECRET_NTFY_TOPIC
#define NOTIFY_DAILY_HOUR    8

/* ================= Пороги аварийных уведомлений ================= */
#define ALERT_HOME_TEMP_C     28.0f
#define ALERT_SERVER_CPU_PCT  85.0f
#define ALERT_SERVER_TEMP_C   75.0f
#define ALERT_PC_CPU_PCT      90.0f
#define ALERT_PC_TEMP_C       80.0f

/* ================= Интервалы ================= */
#define SENSOR_UPDATE_MS    (30UL * 1000UL)
#define UI_TICK_MS          5UL

/* ================= Экран ================= */
#define LCD_H_RES           240
#define LCD_V_RES           240
#define DEFAULT_BRIGHTNESS  200

/* ================= Кнопка ================= */
#define BTN_LONGPRESS_MS    600UL
#define BTN_DEBOUNCE_MS     30UL