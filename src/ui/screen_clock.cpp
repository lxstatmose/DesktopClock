#include "screens.h"
#include "theme.h"
#include "weather_icon.h"

#include "app_state.h"

#include <Arduino.h>
#include <time.h>

static lv_obj_t *s_scr;
static lv_obj_t *s_top;      /* строка сверху: иконка + уличная температура */
static lv_obj_t *s_icon;
static lv_obj_t *s_topTemp;
static lv_obj_t *s_time;
static lv_obj_t *s_date;
static lv_obj_t *s_notice;   /* баннер уведомления от бота */
static uint32_t s_noticeUntil;   /* 0 = не показывать */
static uint32_t s_noticeShownId; /* id последнего показанного (0 = ни одного) */

/* Баннер уведомления: всплывает поверх экрана на NOTICE_SHOW_MS, затем
 * сам скрывается. Новые сообщения приходят с хаба (metrics_fetch). */
#define NOTICE_SHOW_MS 12000UL

static int s_lastSec = -1;

/* Одинаковый отступ погоды (сверху) и даты (снизу) от блока времени. */
#define CLOCK_GAP 24

/* Внутри бокса 40×40 контент иконки смещён вниз (облако/осадки рисуются
 * ниже центра), поэтому визуально иконка «проседает» относительно строки
 * температуры. Поднимаем её на ICON_DY px, чтобы центры совпали. */
#define ICON_DY 3

/* Время — строго в центре экрана, погода и дата — симметрично
 * относительно времени (одинаковое расстояние сверху и снизу).
 * Повторно вызывается после смены текста: размеры лейблов меняются. */
static void clock_layout(void) {
    lv_obj_center(s_time);
    lv_obj_align_to(s_top,  s_time, LV_ALIGN_OUT_TOP_MID,    0, -CLOCK_GAP);
    lv_obj_align_to(s_date, s_time, LV_ALIGN_OUT_BOTTOM_MID, 0,  CLOCK_GAP);
}

lv_obj_t *scr_clock_create(lv_obj_t *parent) {
    s_scr = lv_obj_create(parent);
    ui_screen_style(s_scr);

    /* --- время: крупно, ровно по центру --- */
    s_time = ui_label(s_scr, &lv_font_roboto_72, COL_TEXT, "--:--");

    /* --- сверху: уличная температура (иконка + температура одной строкой) --- */
    s_top = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_top);
    lv_obj_set_scrollable(s_top, false);
    lv_obj_set_flex_flow(s_top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_top, 10, 0);
    lv_obj_set_size(s_top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    s_icon    = weather_icon_create(s_top, 40);
    s_topTemp = ui_label(s_top, &lv_font_roboto_28, COL_TEXT, "—");

    /* выравнивание по вертикали: центр иконки => центр строки температуры */
    lv_obj_set_style_translate_y(s_icon, -ICON_DY, 0);

    /* --- дата: снизу от времени, крупно --- */
    s_date = ui_label(s_scr, &lv_font_roboto_28, COL_TEXT_DIM, "—");

    /* --- баннер уведомления от бота (изначально скрыт) --- */
    s_notice = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_notice);
    lv_obj_set_size(s_notice, 176, 46);
    lv_obj_align(s_notice, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_bg_color(s_notice, COL_CARD, 0);
    lv_obj_set_style_bg_opa(s_notice, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_notice, 10, 0);
    lv_obj_set_style_border_width(s_notice, 1, 0);
    lv_obj_set_style_border_color(s_notice, COL_ACCENT, 0);
    lv_obj_set_scrollable(s_notice, false);
    lv_obj_set_hidden(s_notice, true);

    lv_obj_t *nt = ui_label(s_notice, &lv_font_roboto_14, COL_TEXT, "");
    lv_obj_center(nt);
    lv_obj_set_width(nt, 162);
    lv_obj_set_style_text_align(nt, LV_TEXT_ALIGN_CENTER, 0);

    clock_layout();

    return s_scr;
}

void scr_clock_update(void) {
    /* время/дата — раз в секунду */
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    if (t.tm_sec != s_lastSec) {
        s_lastSec = t.tm_sec;

        char b[8];
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        lv_label_set_text(s_time, b);

        static const char *wd[7] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
        static const char *mo[12] = {"янв", "фев", "мар", "апр", "мая", "июн",
                                     "июл", "авг", "сен", "окт", "ноя", "дек"};
        char d[40];
        snprintf(d, sizeof(d), "%s, %d %s", wd[t.tm_wday % 7],
                 t.tm_mday, mo[t.tm_mon % 12]);
        lv_label_set_text(s_date, d);

        /* размер текста мог измениться — вернуть симметрию */
        clock_layout();
    }

    /* погода — раз в 3 секунды */
    static uint32_t lastW = 0;
    if (millis() - lastW >= 3000) {
        lastW = millis();

        state_lock();
        WeatherData w = g_state.weather;
        state_unlock();

        if (w.valid) {
            char b[16];
            snprintf(b, sizeof(b), "%.0f°", (double)w.tempC);
            lv_label_set_text(s_topTemp, b);
            weather_icon_set(s_icon, w.code, w.isDay);

            /* ширина строки погоды могла измениться — вернуть симметрию */
            clock_layout();
        }
    }

    /* Уведомления от бота: показываем текст на NOTICE_SHOW_MS, потом прячем.
     * Повторно НЕ показываем: отслеживаем id последнего показанного
     * (id монотонно растёт на хабе), таймер заведомо не перезаводится
     * на том же сообщении. Данные кладёт metrics_fetch() из поля
     * "notice" ответа хаба. */
    state_lock();
    const Notice n = g_state.notice;
    state_unlock();

    if (n.valid && n.id != s_noticeShownId) {
        s_noticeShownId = n.id;
        lv_obj_t *nl = lv_obj_get_child(s_notice, 0);   /* label внутри баннера */
        if (nl) lv_label_set_text(nl, n.text);
        lv_obj_set_hidden(s_notice, false);
        s_noticeUntil = millis() + NOTICE_SHOW_MS;
    } else if (s_noticeUntil && (int32_t)(millis() - s_noticeUntil) >= 0) {
        lv_obj_set_hidden(s_notice, true);
        s_noticeUntil = 0;
    }
}
