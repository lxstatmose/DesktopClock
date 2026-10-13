#include "screens.h"
#include "theme.h"
#include "weather_icon.h"
#include "ring_gauge.h"

#include "app_state.h"
#include "app_config.h"
#include "services/weather.h"

#include <Arduino.h>
#include <math.h>

static lv_obj_t *s_scr;
static lv_obj_t *s_gauge;
static lv_obj_t *s_icon;
static lv_obj_t *s_temp;
static lv_obj_t *s_cond;
static lv_obj_t *s_line1;
static lv_obj_t *s_line2;

/* Внутренний радиус кольцевой шкалы — 107 px (116 внешний − 9 толщина);
 * текст держим с запасом внутри 104 px от центра экрана. */
#define RING_TEXT_R  104

/* Выровнять ряд по центру со сдвигом offY и, если его углы всё равно
 * не влезают в круг (широкий текст), подтянуть к центру — ничего
 * не должно заходить на кольцевой график. */
static void fit_ring(lv_obj_t *o, int offY) {
    lv_obj_align(o, LV_ALIGN_CENTER, 0, offY);

    const int32_t w  = lv_obj_get_width(o);
    const int32_t h  = lv_obj_get_height(o);
    const int32_t dx = (w + 1) / 2;
    const int32_t cy = LCD_V_RES / 2;
    if (dx >= RING_TEXT_R) return;      /* и так не влезает — не двигаем */

    const int32_t lim =
        (int32_t)sqrtf((float)(RING_TEXT_R * RING_TEXT_R - dx * dx));
    int32_t y = lv_obj_get_y(o);
    if (y + h > cy + lim) y = cy + lim - h;   /* низ упирается в круг */
    if (y < cy - lim)      y = cy - lim;       /* верх упирается в круг */
    if (y != lv_obj_get_y(o)) lv_obj_set_y(o, y);
}

static void layout_texts(void) {
    lv_obj_update_layout(s_scr);   /* размеры подписей от актуального текста */

    fit_ring(s_temp,  -25);   /* иконка 20..60, температура ниже с отступом ~8  */
    fit_ring(s_cond,   18);   /* «ощущается» — заметный зазор от температуры    */
    fit_ring(s_line1,  40);
    fit_ring(s_line2,  60);
}

lv_obj_t *scr_weather_create(lv_obj_t *parent) {
    s_scr = lv_obj_create(parent);
    ui_screen_style(s_scr);

    /* шкалы-полоски вдоль края круглого экрана:
     * верх — температура, низ — влажность */
    s_gauge = ring_gauge_create(s_scr, LCD_H_RES);
    lv_obj_center(s_gauge);

    /* иконка — высоко */
    s_icon = weather_icon_create(s_scr, 40);
    lv_obj_align(s_icon, LV_ALIGN_TOP_MID, 0, 20);

    /* температура и состояние — по центру; отступы между иконкой,
     * температурой и «ощущается» заметно больше, чем были (см. layout_texts) */
    s_temp = ui_label(s_scr, &lv_font_roboto_48, COL_TEXT, "—");
    lv_obj_align(s_temp, LV_ALIGN_CENTER, 0, -25);

    s_cond = ui_label(s_scr, &lv_font_roboto_16, COL_ACCENT, "—");
    lv_obj_align(s_cond, LV_ALIGN_CENTER, 0, 18);

    /* данные — под температурой, ярким текстом */
    s_line1 = ui_label(s_scr, &lv_font_roboto_14, COL_TEXT, "");
    lv_obj_align(s_line1, LV_ALIGN_CENTER, 0, 40);

    s_line2 = ui_label(s_scr, &lv_font_roboto_14, COL_TEXT, "");
    lv_obj_align(s_line2, LV_ALIGN_CENTER, 0, 60);

    return s_scr;
}

void scr_weather_update(void) {
    static uint32_t last = 0;
    if (millis() - last < 3000) return;
    last = millis();

    state_lock();
    WeatherData w = g_state.weather;
    state_unlock();

    char b[96];

    if (!w.valid) {
        lv_label_set_text(s_temp, "—");
        lv_label_set_text(s_cond, "загрузка…");
        lv_label_set_text(s_line1, "");
        lv_label_set_text(s_line2, "");
        layout_texts();
        /* данных нет — шкалы не показываем (пусто, пока не появятся) */
        ring_gauge_set(s_gauge, -20.0f, -20.0f, 40.0f,
                       0.0f, 0.0f, 100.0f, RING_HUMIDITY);
        return;
    }

    snprintf(b, sizeof(b), "%+.0f°C", (double)w.tempC);
    lv_label_set_text(s_temp, b);

    snprintf(b, sizeof(b), "%s (ощущ. %+.0f°)",
             wmo_short(w.code), (double)w.feelsC);
    lv_label_set_text(s_cond, b);

    snprintf(b, sizeof(b), "Влажн. %d%%  Ветер %.1f м/с",
             w.humidity, (double)w.windMs);
    lv_label_set_text(s_line1, b);

    snprintf(b, sizeof(b), "Давление %.0f мм рт.ст.",
             (double)(w.pressureHpa * 0.750062f));
    lv_label_set_text(s_line2, b);

    layout_texts();

    ring_gauge_set(s_gauge, w.tempC, -20.0f, 40.0f,
                   (float)w.humidity, 0.0f, 100.0f, RING_HUMIDITY);

    weather_icon_set(s_icon, w.code, w.isDay);
}
