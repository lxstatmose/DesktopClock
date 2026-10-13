#include "ring_gauge.h"
#include "theme.h"

#include <string.h>

/*
 * ring_gauge.cpp - шкалы «полосками» вдоль края круглого экрана.
 *
 * Короткие капсулы-сегменты идут строго по дуге окружности, как пунктир по краю:
 *   верхняя полуокружность - температура (синий -> бирюза -> жёлтый -> красный);
 *   нижняя                 - влажность (или давление акцентным цветом).
 *
 * Экран круглый (GC9A01, 240x240, видимая область - окружность), поэтому:
 *   - внешний край шкалы отступлён от кромки панели на RING_INSET px;
 *   - все капсулы лежат внутри этой окружности.
 *
 * ВАЖНО (плавность переходов между экранами): сегменты рисуются НЕ объектами
 * lv_arc, а одним объектом с LV_EVENT_DRAW_MAIN, который рисует их линиями
 * (lv_draw_line с округлёнными концами - визуально та же капсула).
 * Каждый lv_arc прогонял маски (угол + два радиуса) по области 240x240, и
 * 36 таких сегментов добавляли к кадру ~30 мс: переходы с такими экранами
 * шли в 5-10 fps и выглядели дёргано. Линия считает маску только по своему
 * маленькому bbox.
 */

#define RING_SEGS     18   /* сегментов на полуокружность: шаг 180/18 = 10 градусов */
#define RING_GAP_DEG  2    /* зазор между капсулами, градусов                        */
#define RING_THICK    9    /* толщина полоски, px                                    */
#define RING_INSET    4    /* отступ внешнего края шкалы от кромки, px               */

struct RingGauge {
    int       litT;   /* залито сегментов сверху при последнем обновлении */
    int       litB;   /* залито сегментов снизу                            */
    RingBottom mode;
    bool      inited;
};

static float clamp01(float v) {
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

/* Цвет сегмента температурной шкалы: t = 0..1 */
static lv_color_t temp_color(float t) {
    if(t < 0.25f) return lv_color_hex(0x2E86FF);   /* холодно: синий   */
    if(t < 0.50f) return lv_color_hex(0x2ED3B7);   /* прохладно        */
    if(t < 0.75f) return lv_color_hex(0xFFB300);   /* тепло: жёлтый    */
    return lv_color_hex(0xFF5252);                 /* жарко: красный   */
}

/* Цвет сегмента шкалы влажности: сухо -> влажно */
static lv_color_t hum_color(float t) {
    if(t < 0.33f) return lv_color_hex(0xFFB300);   /* сухо: янтарь     */
    if(t < 0.66f) return lv_color_hex(0x7EC8E3);   /* нормально        */
    return lv_color_hex(0x3EA6FF);                 /* влажно: синий    */
}

/* Цвет сегмента температурной шкалы (для нижней половины): зелёный -> жёлтый -> оранжевый -> красный */
static lv_color_t temp_color_bottom(float t) {
    if(t < 0.25f) return lv_color_hex(0x00CC00);   /* холодно: зелёный */
    if(t < 0.50f) return lv_color_hex(0xFFD600);   /* тепло: жёлтый    */
    if(t < 0.75f) return lv_color_hex(0xFF9100);   /* жарко: оранжевый */
    return lv_color_hex(0xFF1744);                 /* очень жарко: красный */
}

/* Одна капсула-сегмент: короткая линия по хорде сектора с закруглёнными
 * концами (тот же вид, что давал lv_arc), но с ценой пропорциональной
 * её маленькому bbox, а не всему экрану. */
static void dash(lv_layer_t *layer, int cx, int cy, int r,
                 int a0, int a1, lv_color_t color, lv_opa_t opa) {
    /* Углы LVGL: 0 = вправо, дальше по часовой стрелке (Y вниз). */
    const int32_t x0 = cx + ((lv_trigo_cos((int16_t)a0) * r) >> LV_TRIGO_SHIFT);
    const int32_t y0 = cy + ((lv_trigo_sin((int16_t)a0) * r) >> LV_TRIGO_SHIFT);
    const int32_t x1 = cx + ((lv_trigo_cos((int16_t)a1) * r) >> LV_TRIGO_SHIFT);
    const int32_t y1 = cy + ((lv_trigo_sin((int16_t)a1) * r) >> LV_TRIGO_SHIFT);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;
    dsc.color       = color;
    dsc.width       = RING_THICK;
    dsc.opa         = opa;
    dsc.round_start = 1;   /* скруглённые торцы -> «капсула» */
    dsc.round_end   = 1;
    lv_draw_line(layer, &dsc);
}

/* Отрисовка шкалы: 18 сегментов верхней дуги + 18 нижней.
 * Рисуем ТОЛЬКО залитые сегменты: незанятая часть дуги не показывается
 * вовсе и «появляется» по мере того, как значение реально доходит до неё. */
static void ring_draw_cb(lv_event_t *e) {
    lv_obj_t *box     = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    RingGauge *p      = (RingGauge *)lv_event_get_user_data(e);
    if(!p || !layer) return;

    /* Радиус минус отступ: внешний край полоски не доходит до кромки. */
    const int size = lv_obj_get_width(box);
    const int cx = size / 2;
    const int cy = size / 2;
    const int r  = cx - RING_INSET;

    const int step = 180 / RING_SEGS;   /* 10 градусов - целые углы */
    const int gap2 = RING_GAP_DEG / 2;

    for(int i = 0; i < RING_SEGS; i++) {
        const float ft = (float)i / (float)(RING_SEGS - 1);

        /* Верх: 180 -> 360 (слева направо через верх) */
        if (i < p->litT) {
            lv_color_t ct;
            if (p->mode == RING_TEMP) ct = temp_color_bottom(ft);
            else if (p->mode == RING_SERVER) ct = temp_color(ft);  /* CPU% для сервера */
            else ct = temp_color(ft);
            dash(layer, cx, cy, r,
                 180 + i * step + gap2, 180 + (i + 1) * step - gap2,
                 ct, LV_OPA_COVER);
        }

        /* Низ: 0 -> 180 (справа налево через низ) */
        if (i < p->litB) {
            lv_color_t cb;
            if (p->mode == RING_HUMIDITY) cb = hum_color(ft);
            else if (p->mode == RING_TEMP) cb = temp_color_bottom(ft);
            else if (p->mode == RING_SERVER) cb = temp_color_bottom(ft);  /* CPU temp для сервера */
            else cb = COL_ACCENT;
            dash(layer, cx, cy, r,
                 i * step + gap2, (i + 1) * step - gap2,
                 cb, LV_OPA_COVER);
        }
    }
}

static void ring_gauge_delete_cb(lv_event_t *e) {
    RingGauge *p = (RingGauge *)lv_event_get_user_data(e);
    if(p) lv_free(p);
}

lv_obj_t *ring_gauge_create(lv_obj_t *parent, int size) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_scrollable(box, false);

    RingGauge *p = (RingGauge *)lv_malloc(sizeof(RingGauge));
    memset(p, 0, sizeof(RingGauge));
    lv_obj_set_user_data(box, p);
    lv_obj_add_event_cb(box, ring_gauge_delete_cb, LV_EVENT_DELETE, p);
    lv_obj_add_event_cb(box, ring_draw_cb, LV_EVENT_DRAW_MAIN, p);

    return box;
}

void ring_gauge_set(lv_obj_t *gauge,
                    float temp, float tempMin, float tempMax,
                    float bot,  float botMin, float botMax,
                    RingBottom mode) {
    RingGauge *p = (RingGauge *)lv_obj_get_user_data(gauge);
    if(!p) return;

    const float t = clamp01((temp - tempMin) / (tempMax - tempMin));
    const float b = clamp01((bot - botMin) / (botMax - botMin));

    const int litT = (int)(t * RING_SEGS + 0.5f);
    const int litB = (int)(b * RING_SEGS + 0.5f);

    /* Перерисовываем только при реальном изменении: invalidate задевает весь
     * бокс (то есть весь экран), поэтому без надобности его не зовём. */
    if(p->inited && p->litT == litT && p->litB == litB && p->mode == mode) return;
    p->litT   = litT;
    p->litB   = litB;
    p->mode   = mode;
    p->inited = true;

    lv_obj_invalidate(gauge);
}