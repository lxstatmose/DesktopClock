#pragma once
/*
 * theme.h — тёмная палитра и вспомогательные функции UI.
 */
#include <lvgl.h>

#define COL_BG        lv_color_hex(0x0B0F14)
#define COL_CARD      lv_color_hex(0x151C24)
#define COL_ACCENT    lv_color_hex(0x3EA6FF)
#define COL_ACCENT2   lv_color_hex(0xD8DEE4)  /* был оранжевый — теперь светло-серый */
#define COL_TEMP      lv_color_hex(0xC9D1D9)  /* был оранжевый — теперь светло-серый */
#define COL_TEXT      lv_color_hex(0xE6EDF3)
#define COL_TEXT_DIM  lv_color_hex(0x8B98A5)
#define COL_CLOUD     lv_color_hex(0x9AA7B2)

/* Прозрачный контейнер заданного размера по центру родителя. */
lv_obj_t *ui_box(lv_obj_t *parent, int w, int h);

/* Общий стиль круглого экрана. */
void ui_screen_style(lv_obj_t *scr);

/* Быстрое создание подписи с нужным шрифтом/цветом и выравниванием. */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                   const char *text);
