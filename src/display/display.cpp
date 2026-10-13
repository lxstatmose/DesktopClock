#include "display.h"

#include "lgfx_config.hpp"
#include "app_config.h"
#include "pin_config.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

/* Единственный экземпляр LovyanGFX (объявлен extern в lgfx_config.hpp) */
LGFX lcd;

static lv_display_t *s_disp = nullptr;
static uint8_t      *s_buf[2] = {nullptr, nullptr};
static bool          s_pipelined = false;

/* Частичный буфер: 80 строк => 240*80*2 = 38 400 байт на буфер.
 * Два буфера (~77 КБ суммарно, внутренняя RAM) — рендер и DMA идут
 * параллельно, а размер буфера подобран так, чтобы на полный кадр 240×240
 * приходилось 3 чанка: это заметно быстрее 5 чанков (меньше накладных
 * расходов на границы) и быстрее 2 крупных (хуже перекрытие рендера и DMA).
 * Если второй не выделится — LVGL работает с одним (s_pipelined = false,
 * синхронный flush). */
#define DISP_BUF_LINES 80u

/* ---------- LVGL tick ---------- */
static uint32_t lv_tick_cb(void) {
    return millis();
}


/* ---------- Flush: LVGL -> LovyanGFX (DMA) ---------- */
/* Два буфера + ожидание DMA в НАЧАЛЕ flush: LVGL рендерит следующий чанк
 * во втором буфере, пока SPI отправляет предыдущий — кадр (и слайды экранов)
 * собирается заметно быстрее, чем при синхронной отправке. */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    /* Буфер LVGL имеет формат LV_COLOR_FORMAT_RGB565_SWAPPED —
     * ровно то, что ожидает pushImage() у LovyanGFX. */
    if (s_pipelined) {
        /* Дождаться прошлой DMA-передачы: к этому моменту чанк, лежащий
         * в ЭТОМ буфере (он отправлялся на шаг раньше), уже ушёл на дисплей. */
        lcd.waitDMA();
        lcd.pushImageDMA((int32_t)area->x1, (int32_t)area->y1, (int32_t)w, (int32_t)h,
                         (uint16_t *)px_map);
        lv_display_flush_ready(disp);   /* LVGL может сразу рендерить след. чанк */
    } else {
        /* Одиночный буфер: нельзя отдавать буфер LVGL, пока DMA его читает. */
        lcd.pushImageDMA((int32_t)area->x1, (int32_t)area->y1, (int32_t)w, (int32_t)h,
                         (uint16_t *)px_map);
        lcd.waitDMA();
        lv_display_flush_ready(disp);
    }
}

void display_init() {
    /* ---- Панель ---- */
    lcd.init();
    lcd.initDMA();
    lcd.setRotation(0);
    lcd.setBrightness(DEFAULT_BRIGHTNESS);
    lcd.startWrite();
    lcd.fillScreen(0x0000);   /* чёрный */

    /* ---- LVGL ---- */
    lv_init();
    lv_tick_set_cb(lv_tick_cb);

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    /* Два частичных буфера (DMA, внутренняя RAM) для параллельного
     * рендера/отправки; при нехватке RAM остаёмся с одним. */
    const size_t chunk_bytes = (size_t)LCD_H_RES * DISP_BUF_LINES * sizeof(uint16_t);
    s_buf[0] = (uint8_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf[1] = (uint8_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf[0])
        s_buf[0] = (uint8_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);
    if (!s_buf[1])
        s_buf[1] = (uint8_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA);

    if (!s_buf[0]) {
        Serial.println("[display] FATAL: не удалось выделить буфер отрисовки, останов");
        while (true) {
            delay(1000);
        }
    }
    s_pipelined = (s_buf[1] != nullptr);
    if (!s_pipelined) {
        Serial.println("[display] второй буфер не выделен — синхронный flush");
    }

    lv_display_set_flush_cb(s_disp, disp_flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(s_disp, s_buf[0], s_buf[1], chunk_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void display_set_brightness(uint8_t value) {
    lcd.setBrightness(value);
}

lv_display_t *display_get() {
    return s_disp;
}
