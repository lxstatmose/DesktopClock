#include "screens.h"
#include "theme.h"
#include "ring_gauge.h"

#include "app_config.h"
#include "app_state.h"

#include <Arduino.h>

/* Экран «Система»: IP, WiFi, погода, аптайм, RAM, температура ESP.
 * Оформление — как на остальных экранах: кольцевая шкала по краю
 * (верх — температура чипа, низ — сила WiFi) и аккуратный список
 * «метка → значение» в центре, со статусами, подсвеченными цветом. */

#define SYS_ROWS 6

static lv_obj_t *s_scr;
static lv_obj_t *s_gauge;
static lv_obj_t *s_val[SYS_ROWS];     /* только значения, метки статичны */

/* RSSI dBm → %. Шкала честная: -30 dBm (отлично) = 100%, -90 = 0%. */
static int rssi_to_pct(int8_t rssi) {
    if (!rssi) return 0;
    int p = ((int)rssi + 90) * 100 / 60;
    if (p < 0)   p = 0;
    if (p > 100) p = 100;
    return p;
}

static void fmt_uptime(uint32_t sec, char *out, size_t n) {
    const uint32_t d = sec / 86400UL;
    const uint32_t h = (sec % 86400UL) / 3600UL;
    if (d > 0)      snprintf(out, n, "%u д %u ч", (unsigned)d, (unsigned)h);
    else if (h > 0) snprintf(out, n, "%u ч %u м", (unsigned)h, (unsigned)((sec % 3600UL) / 60UL));
    else            snprintf(out, n, "%u мин", (unsigned)(sec / 60UL));
}

/* Одна строка списка: метка слева (dim), значение справа (bright). */
static lv_obj_t *sys_row(lv_obj_t *parent, const char *key) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    ui_label(row, &lv_font_roboto_14, COL_TEXT_DIM, key);
    return ui_label(row, &lv_font_roboto_14, COL_TEXT, "—");
}

lv_obj_t *scr_system_create(lv_obj_t *parent) {
    s_scr = lv_obj_create(parent);
    ui_screen_style(s_scr);

    /* кольцевая шкала: верх — температура ESP, низ — сила WiFi */
    s_gauge = ring_gauge_create(s_scr, LCD_H_RES);
    lv_obj_center(s_gauge);

    lv_obj_t *title = ui_label(s_scr, &lv_font_roboto_14, COL_TEXT_DIM, "Система");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    /* --- список «метка … значение» ---
     * Ширина 140 подобрана так, чтобы и сам список, и его строки
     * помещались внутрь кольцевой шкалы на всех высотах. */
    lv_obj_t *list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 140, LV_SIZE_CONTENT);
    lv_obj_center(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_scrollable(list, false);

    s_val[0] = sys_row(list, "IP");
    s_val[1] = sys_row(list, "WiFi");
    s_val[2] = sys_row(list, "Погода");
    s_val[3] = sys_row(list, "Аптайм");
    s_val[4] = sys_row(list, "RAM");
    s_val[5] = sys_row(list, "ESP");

    return s_scr;
}

void scr_system_update(void) {
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();

    state_lock();
    SystemInfo s   = g_state.sys;
    const bool wOk = g_state.weather.valid;
    state_unlock();

    char b[48];

    /* IP */
    lv_label_set_text(s_val[0], s.wifiOnline ? s.ip : "—");
    lv_obj_set_style_text_color(s_val[0],
        s.wifiOnline ? COL_TEXT : COL_TEXT_DIM, 0);

    /* WiFi: сила сигнала цветом — зелёный/жёлтый/красный */
    if (s.wifiOnline) {
        const int pct = rssi_to_pct(s.rssi);
        snprintf(b, sizeof(b), "%d dBm · %d%%", (int)s.rssi, pct);
        lv_label_set_text(s_val[1], b);
        lv_color_t c = COL_TEXT;
        if (pct >= 60)      c = lv_color_hex(0x3FB950);
        else if (pct >= 30) c = lv_color_hex(0xFFB300);
        else                c = lv_color_hex(0xFF5252);
        lv_obj_set_style_text_color(s_val[1], c, 0);
    } else {
        lv_label_set_text(s_val[1], "нет связи");
        lv_obj_set_style_text_color(s_val[1], lv_color_hex(0xFF5252), 0);
    }

    /* Погода */
    lv_label_set_text(s_val[2], wOk ? "OK" : "нет данных");
    lv_obj_set_style_text_color(s_val[2],
        wOk ? lv_color_hex(0x3FB950) : COL_TEXT_DIM, 0);

    /* Аптайм */
    char up[24];
    fmt_uptime(s.uptimeSec, up, sizeof(up));
    lv_label_set_text(s_val[3], up);
    lv_obj_set_style_text_color(s_val[3], COL_TEXT, 0);

    /* RAM */
    snprintf(b, sizeof(b), "%lu КБ", (unsigned long)(s.freeHeap / 1024UL));
    lv_label_set_text(s_val[4], b);
    lv_obj_set_style_text_color(s_val[4], COL_TEXT, 0);

    /* Температура чипа: зелёный в норме, красный при перегреве */
    snprintf(b, sizeof(b), "%.0f°C", (double)s.chipTempC);
    lv_label_set_text(s_val[5], b);
    lv_obj_set_style_text_color(s_val[5],
        s.chipTempC < 60.0f ? COL_TEXT : lv_color_hex(0xFF5252), 0);

    /* --- кольцевая шкала --- */
    const float wifiPct = s.wifiOnline ? (float)rssi_to_pct(s.rssi) : 0.0f;
    ring_gauge_set(s_gauge, s.chipTempC, 20.0f, 70.0f,
                   wifiPct, 0.0f, 100.0f, RING_PRESSURE);
}
