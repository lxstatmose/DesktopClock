#pragma once
/*
 * app_state.h — общее состояние приложения (заполняет net/sensor-задача,
 * читает UI). Доступ защищён мьютексом.
 */
#include <Arduino.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* -------- Погода -------- */
struct WeatherData {
    bool     valid;
    float    tempC;
    float    feelsC;
    float    windMs;
    int      windDir;
    int      humidity;
    float    pressureHpa;
    int      code;            /* WMO weather code */
    int      isDay;
    int      sunriseMin;      /* минуты от полуночи */
    int      sunsetMin;
    uint32_t updatedMs;
};

/* -------- Дом (AHT21 + ENS160) -------- */
struct HomeData {
    bool  valid;
    bool  aht21Ok;       /* датчик T/RH доступен */
    bool  ens160Ok;      /* датчик качества воздуха доступен */

    float tempC;
    float humidity;      /* % */

    /* ENS160: всё, что он умеет отдавать */
    float    eco2;       /* ppm  (эквивалент CO2)                  */
    float    etvoc;      /* ppb (общие леткие органические соединения) */
    uint8_t  aqi;        /* AQI UBA: 1 отлично .. 5 вредно          */
    uint8_t  ensStatus;  /* 0 норма, 1 прогрев, 2 первый запуск, 3 нет вывода */
    uint32_t rs[4];      /* сопротивления газовых каналов, Ом (Rs0..Rs3) */
    uint8_t  fwVer[3];   /* версия прошивки ENS160 {major, minor, release} */
};

/* -------- Система (ESP + статус сервисов) -------- */
struct SystemInfo {
    char     ip[20];
    int8_t   rssi;
    uint32_t uptimeSec;
    bool     wifiOnline;
    uint32_t freeHeap;
    float    chipTempC;   /* внутренний температурный датчик ESP (°C) */
};

/* -------- Метрики внешнего устройства (сервер / ПК) --------
 * Их собирает агент на самой машине и отдаёт нашему хабу на сервере;
 * плата забирает готовые значения по HTTP и рисует на отдельном экране.
 * Пока агент не ответил — valid = false, экран показывает «нет данных». */
struct DeviceMetrics {
    bool     valid;
    char     name[20];        /* «Сервер», «ПК» (заголовки экранов статичны) */
    float    cpuPct;           /* загрузка ЦП, %                        */
    float    gpuPct;           /* загрузка GPU, % (ПК)                  */
    uint32_t memUsedMb;
    uint32_t memTotalMb;
    float    tempC;            /* CPU температура                       */
    float    gpuTempC;         /* GPU температура (ПК)                  */
    float    diskTempC;        /* SSD/HDD температура                   */
    float    load1;            /* load average (Linux), -1 = нет        */
    uint32_t uptimeSec;        /* аптайм самой машины                   */
    uint32_t updatedMs;        /* millis() на момент получения          */
};

/* -------- Уведомление из Telegram (пришло через серверный бот) --------
 * Бот не может достучаться до платы напрямую, поэтому он кладёт текст в
 * очередь на сервере, а плата забирает очередь по HTTP и показывает
 * поверх экрана часов. */
struct Notice {
    bool     valid;
    uint32_t id;              /* растёт на сервере: новые = большие id  */
    char     text[96];
    uint32_t createdMs;
};

struct AppState {
    WeatherData   weather;
    HomeData      home;
    SystemInfo    sys;
    DeviceMetrics server;
    DeviceMetrics pc;
    Notice        notice;      /* последнее непрочитанное уведомление */
};

extern AppState          g_state;
extern SemaphoreHandle_t g_stateMutex;

void state_init();
void state_lock();
void state_unlock();

/* RAII-обёртка для безопасного захвата мьютекса состояния */
class StateGuard {
public:
    StateGuard()  { state_lock(); }
    ~StateGuard() { state_unlock(); }
    StateGuard(const StateGuard &) = delete;
    StateGuard &operator=(const StateGuard &) = delete;
};