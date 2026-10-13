#pragma once
/*
 * screens.h — четыре экрана UI.
 * Каждый экран: create(parent) один раз, update() — периодически.
 * create() принимает родителя (корневой контейнер ui_manager), чтобы
 * экраны были обычными объектами и участвовали в слайд-переходах.
 */
#include <lvgl.h>

lv_obj_t *scr_clock_create(lv_obj_t *parent);
void      scr_clock_update(void);

lv_obj_t *scr_weather_create(lv_obj_t *parent);
void      scr_weather_update(void);

lv_obj_t *scr_home_create(lv_obj_t *parent);
void      scr_home_update(void);

lv_obj_t *scr_server_create(lv_obj_t *parent);
void      scr_server_update(void);

lv_obj_t *scr_pc_create(lv_obj_t *parent);
void      scr_pc_update(void);

lv_obj_t *scr_system_create(lv_obj_t *parent);
void      scr_system_update(void);
