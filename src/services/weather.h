#pragma once
/*
 * weather.h — погода с OpenWeatherMap (Current Weather API, HTTPS).
 */
#include <Arduino.h>

/* Загрузить текущую погоду и заполнить g_state.weather. true — успех. */
bool weather_update();

/* WMO weather code -> описание на русском. */
const char *wmo_text(int code);

/* Короткое описание (для экрана «Часы»). */
const char *wmo_short(int code);
