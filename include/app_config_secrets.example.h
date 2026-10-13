#pragma once
/*
 * app_config_secrets.example.h — шаблон секретов.
 * Скопируйте этот файл в app_config_secrets.h и заполните значениями.
 */

#ifndef APP_CONFIG_SECRETS_H
#define APP_CONFIG_SECRETS_H

/* ================= WiFi ================= */
#define SECRET_WIFI_SSID           "YOUR_WIFI_SSID"
#define SECRET_WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"

/* ================= SOCKS5 прокси (роутер) ================= */
#define SECRET_SOCKS5_HOST         "192.168.2.1"
#define SECRET_SOCKS5_PORT         2080

/* ================= Погода (OpenWeatherMap) ================= */
#define SECRET_WEATHER_API_KEY     "YOUR_OPENWEATHERMAP_API_KEY"

/* ================= Хаб метрик (домашний сервер) ================= */
#define SECRET_METRICS_HUB_HOST    "192.168.2.115"
#define SECRET_METRICS_HUB_PORT    8082

/* ================= Уведомления ntfy.sh ================= */
#define SECRET_NTFY_TOPIC          "your-unique-topic-name"

#endif // APP_CONFIG_SECRETS_H