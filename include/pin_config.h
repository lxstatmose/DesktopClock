#pragma once
/*
 * pin_config.h — карта выводов для ESP32-S3 SuperMini
 *
 * Безопасные GPIO у SuperMini: 1, 2, 4..8, 9..11, 15..18, 21, 33..38
 * НЕ использовать: 3, 45, 46 (strapping), 19/20 (native USB),
 *                  43/44 (UART0 / boot-консоль), 48 (встроенный WS2812)
 */

/* ---------- GC9A01 (SPI) ---------- */
#define PIN_LCD_SCLK        4
#define PIN_LCD_MOSI        6
#define PIN_LCD_DC          5
#define PIN_LCD_CS          7
#define PIN_LCD_RST         8
#define PIN_LCD_BL          10

/* У некоторых модулей подсветка включается низким уровнем.
 * 1 — активный ВЫСОКИЙ (обычный случай), 0 — активный НИЗКИЙ. */
#define LCD_BL_ACTIVE_HIGH  1

/* ---------- AHT21 (I2C: температура + влажность) ---------- */
#define PIN_I2C_SDA         1
#define PIN_I2C_SCL         2
#define AHT21_I2C_ADDR      0x38

/* ---------- ENS160 (I2C: eCO2, eTVOC) ---------- */
#define ENS160_I2C_ADDR     0x59

/* ---------- Свободный пин (бывший RNG для SSLClient) ---------- */
/* GPIO9 свободен и не является strap — ранее использовался
 * для analogRead()-энтропии SSLClient. Сейчас не занят. */
#define PIN_RNG_ANALOG      9

/* ---------- Кнопка TTP223 (ёмкостная) ---------- */
#define PIN_BUTTON          11
/* TTP223 при касании выдаёт ВЫСОКИЙ уровень => active LOW = 0.
 * Для обычной тактильной кнопки с подтяжкой к 3V3 поставь 1 и PULLUP=1. */
#define BUTTON_ACTIVE_LOW   0
#define BUTTON_USE_PULLUP   0
