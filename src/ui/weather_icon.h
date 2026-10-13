#pragma once
/*
 * weather_icon.h — векторная иконка погоды, собранная из примитивов LVGL
 * (встроенные шрифты не содержат символы погоды).
 */
#include <lvgl.h>

lv_obj_t *weather_icon_create(lv_obj_t *parent, int size);
void      weather_icon_set(lv_obj_t *icon, int wmoCode, int isDay);
