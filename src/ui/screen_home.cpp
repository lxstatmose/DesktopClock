#include "screens.h"
#include "theme.h"
#include "ring_gauge.h"

#include "app_config.h"
#include "app_state.h"

#include <Arduino.h>

/* Экран «Дом»: AHT21 (T/RH) + ENS160 (eCO2, TVOC, AQI).
 *
 * Типографика — единая сетка, как на остальных экранах:
 *   roboto_28 — температура (единственный крупный «герой»);
 *   roboto_20 — eCO2 (главное показание блока «Воздух»);
 *   roboto_16 — всё второстепенное (влажность, TVOC, AQI).
 *
 * У КАЖДОГО показания ENS160 (eCO2, TVOC, AQI) есть словесная оценка и
 * цвет «светофора» по единой шкале 1..5 — иначе по цифрам («452 ppm»)
 * непонятно, это норма или уже душно. Между блоками «Климат» и «Воздух» —
 * разделитель, стоящий точно посередине зазора (равные отступы сверху/снизу).
 *
 * ВАЖНО: в шрифте Roboto нет символа «·» (U+00B7) — вместо него
 * плейсхолдер-квадрат. Разделителем служит «•» (U+2022), он в шрифте есть. */

/* Внутренний радиус кольцевой шкалы — 104 px от центра (как на «Погоде»). */
#define RING_TEXT_R  104

/* Разделитель значений внутри строки. U+2022 (bullet) — есть в шрифте. */
#define SEP " • "

static lv_obj_t *s_scr;
static lv_obj_t *s_gauge;
static lv_obj_t *s_title;
static lv_obj_t *s_temp;
static lv_obj_t *s_humid;
static lv_obj_t *s_rule;
static lv_obj_t *s_co2;
static lv_obj_t *s_tvoc;
static lv_obj_t *s_aqi;

/* Единая шкала качества 1..5 для всех показаний ENS160. */
static lv_color_t level_color(uint8_t lv) {
    switch (lv) {
        case 1: return lv_color_hex(0x3FB950);   /* отлично  */
        case 2: return lv_color_hex(0x7EC8E3);   /* хорошо   */
        case 3: return lv_color_hex(0xFFB300);   /* умеренно */
        case 4: return lv_color_hex(0xFF9100);   /* плохо    */
        case 5: return lv_color_hex(0xFF5252);   /* вредно   */
        default: return COL_TEXT_DIM;
    }
}

static const char *level_word(uint8_t lv) {
    switch (lv) {
        case 1:  return "отлично";
        case 2:  return "хорошо";
        case 3:  return "умеренно";
        case 4:  return "плохо";
        case 5:  return "вредно";
        default: return "—";
    }
}

/* Пороги eCO2 (ppm) по общепринятым нормам для помещений:
 *  <600 отлично, <1000 хорошо, <1500 умеренно, <2000 плохо, далее вредно. */
static uint8_t eco2_level(float ppm) {
    if (ppm < 600.0f)  return 1;
    if (ppm < 1000.0f) return 2;
    if (ppm < 1500.0f) return 3;
    if (ppm < 2000.0f) return 4;
    return 5;
}

/* Пороги TVOC (ppb): <150 отлично, <300 хорошо, <500 умеренно,
 * <1000 плохо, далее вредно. */
static uint8_t tvoc_level(float ppb) {
    if (ppb < 150.0f)  return 1;
    if (ppb < 300.0f)  return 2;
    if (ppb < 500.0f)  return 3;
    if (ppb < 1000.0f) return 4;
    return 5;
}

/* ENS160 в фазах прогрева выводит невалидные значения:
 * 1 — прогрев после включения (~3 мин),
 * 2 — первый запуск, установка базовой линии (до 1 ч),
 * 3 — невалидный вывод. */
static const char *ens_status_text(uint8_t st) {
    switch (st) {
        case 1:  return "прогрев…";
        case 2:  return "первый запуск…";
        case 3:  return "нет вывода";
        default: return nullptr;               /* норма */
    }
}

/* Сколько ждать до валидных eCO2/TVOC/AQI. */
static const char *ens_status_hint(uint8_t st) {
    switch (st) {
        case 1:  return "AQI через ~3 мин";
        case 2:  return "AQI через ~1 час";
        default: return "AQI —";
    }
}

lv_obj_t *scr_home_create(lv_obj_t *parent) {
    s_scr = lv_obj_create(parent);
    ui_screen_style(s_scr);

    /* кольцевая шкала по краю: верх — температура, низ — влажность */
    s_gauge = ring_gauge_create(s_scr, LCD_H_RES);
    lv_obj_center(s_gauge);

    s_title = ui_label(s_scr, &lv_font_roboto_14, COL_TEXT_DIM, "Дом");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 18);

    /* --- Блок «Климат» (AHT21) ---
     * Температура — единственная 28px на экране (главный «герой»). */
    s_temp = ui_label(s_scr, &lv_font_roboto_28, COL_TEXT, "—");
    lv_obj_align(s_temp, LV_ALIGN_CENTER, 0, -62);

    s_humid = ui_label(s_scr, &lv_font_roboto_16, COL_TEMP, "—");
    lv_obj_align(s_humid, LV_ALIGN_CENTER, 0, -30);

    /* Разделитель между блоками. Визуально заметный (COL_TEXT_DIM),
     * стоит ровно в середине зазора между влажностью и eCO2. */
    s_rule = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_rule);
    lv_obj_set_size(s_rule, 120, 2);
    lv_obj_align(s_rule, LV_ALIGN_CENTER, 0, -2);
    lv_obj_set_style_bg_color(s_rule, COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_opa(s_rule, LV_OPA_COVER, 0);

    /* --- Блок «Воздух» (ENS160) ---
     * eCO2 — 20px (главное показание блока), остальное 16px. */
    s_co2 = ui_label(s_scr, &lv_font_roboto_20, COL_TEXT, "—");
    lv_obj_align(s_co2, LV_ALIGN_CENTER, 0, 26);

    s_tvoc = ui_label(s_scr, &lv_font_roboto_16, COL_TEXT, "—");
    lv_obj_align(s_tvoc, LV_ALIGN_CENTER, 0, 56);

    s_aqi = ui_label(s_scr, &lv_font_roboto_16, COL_TEXT, "—");
    lv_obj_align(s_aqi, LV_ALIGN_CENTER, 0, 82);

    return s_scr;
}

void scr_home_update(void) {
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();

    state_lock();
    HomeData h = g_state.home;
    state_unlock();

    char b[80];

    if (!h.valid) {
        lv_label_set_text(s_temp, "—");
        lv_label_set_text(s_humid, "нет данных");
        lv_obj_set_style_text_color(s_humid, COL_TEXT_DIM, 0);
        lv_label_set_text(s_co2, "—");
        lv_obj_set_style_text_color(s_co2, COL_TEXT_DIM, 0);
        lv_label_set_text(s_tvoc, "");
        lv_label_set_text(s_aqi, "");
        ring_gauge_set(s_gauge, 0.0f, 0.0f, 40.0f,
                       0.0f, 0.0f, 100.0f, RING_HUMIDITY);
        return;
    }

    /* --- Блок «Климат» (AHT21) --- */
    if (h.aht21Ok) {
        snprintf(b, sizeof(b), "%.1f°C", (double)h.tempC);
        lv_label_set_text(s_temp, b);
        lv_obj_set_style_text_color(s_temp, COL_TEXT, 0);

        if (h.humidity > 0.0f)
            snprintf(b, sizeof(b), "Влажность %.0f%%", (double)h.humidity);
        else
            snprintf(b, sizeof(b), "Влажность —");
        lv_label_set_text(s_humid, b);
        lv_obj_set_style_text_color(s_humid, COL_TEMP, 0);
    } else {
        lv_label_set_text(s_temp, "—");
        lv_obj_set_style_text_color(s_temp, COL_TEXT_DIM, 0);
        lv_label_set_text(s_humid, "нет датчика T/RH");
        lv_obj_set_style_text_color(s_humid, COL_TEXT_DIM, 0);
    }

    /* --- Блок «Воздух» (ENS160) ---
     * Каждое показание — со словесной оценкой и цветом «светофора»,
     * чтобы по цифрам было понятно, хорошо это или плохо. */
    const char *stTxt = ens_status_text(h.ensStatus);
    if (!h.ens160Ok) {
        lv_label_set_text(s_co2, "—");
        lv_obj_set_style_text_color(s_co2, COL_TEXT_DIM, 0);
        lv_label_set_text(s_tvoc, "нет датчика воздуха");
        lv_obj_set_style_text_color(s_tvoc, COL_TEXT_DIM, 0);
        lv_label_set_text(s_aqi, "");
    } else if (stTxt != nullptr) {
        /* Прогрев: чип отвечает, но eCO2/TVOC/AQI ещё не валидны —
         * показываем приглушённо, без оценки. */
        snprintf(b, sizeof(b), h.eco2 > 0.0f ? "%.0f ppm" : "— ppm",
                 (double)h.eco2);
        lv_label_set_text(s_co2, b);
        lv_obj_set_style_text_color(s_co2, COL_TEXT_DIM, 0);

        snprintf(b, sizeof(b), "TVOC %.0f ppb", (double)h.etvoc);
        lv_label_set_text(s_tvoc, b);
        lv_obj_set_style_text_color(s_tvoc, COL_TEXT_DIM, 0);

        lv_label_set_text(s_aqi, ens_status_hint(h.ensStatus));
        lv_obj_set_style_text_color(s_aqi, COL_TEXT_DIM, 0);
    } else {
        const uint8_t cLv = eco2_level(h.eco2);
        const uint8_t tLv = tvoc_level(h.etvoc);

        snprintf(b, sizeof(b), "%.0f ppm" SEP "%s",
                 (double)h.eco2, level_word(cLv));
        lv_label_set_text(s_co2, b);
        lv_obj_set_style_text_color(s_co2, level_color(cLv), 0);

        snprintf(b, sizeof(b), "TVOC %.0f ppb" SEP "%s",
                 (double)h.etvoc, level_word(tLv));
        lv_label_set_text(s_tvoc, b);
        lv_obj_set_style_text_color(s_tvoc, level_color(tLv), 0);

        snprintf(b, sizeof(b), "AQI %u" SEP "%s",
                 h.aqi, level_word(h.aqi));
        lv_label_set_text(s_aqi, b);
        lv_obj_set_style_text_color(s_aqi, level_color(h.aqi), 0);
    }

    /* --- кольцевая шкала: верх температура, низ влажность --- */
    ring_gauge_set(s_gauge, h.tempC, 0.0f, 40.0f,
                   h.humidity, 0.0f, 100.0f, RING_HUMIDITY);
}