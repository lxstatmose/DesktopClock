#pragma once
/*
 * display.h — инициализация LovyanGFX + LVGL (ESP32-S3 + GC9A01 240x240).
 */
#include <lvgl.h>

/* Инициализация дисплея и LVGL. Вызывать один раз в setup(). */
void display_init();

/* Яркость подсветки 0..255. */
void display_set_brightness(uint8_t value);

/* HANDLE дисплея LVGL (нужен ui_manager). */
lv_display_t *display_get();
