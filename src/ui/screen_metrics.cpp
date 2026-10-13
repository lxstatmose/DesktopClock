#include "screens.h"
#include "theme.h"
#include "ring_gauge.h"

#include "app_config.h"
#include "app_state.h"

#include <Arduino.h>
#include <stdio.h>

struct MetricsView {
    lv_obj_t *scr;
    lv_obj_t *title;
    lv_obj_t *gauge;
    lv_obj_t *cpu;
    lv_obj_t *cpu_temp;
    lv_obj_t *gpu;
    lv_obj_t *gpu_temp;
    lv_obj_t *mem;
    lv_obj_t *disk_temp;
    lv_obj_t *up;
    int       lastSec;
    bool      isServer;
};

static MetricsView s_viewServer;
static MetricsView s_viewPc;

static void fmt_uptime(uint32_t sec, char *out, size_t n) {
    const uint32_t d = sec / 86400UL;
    const uint32_t h = (sec % 86400UL) / 3600UL;
    if (d > 0)      snprintf(out, n, "%u д %u ч", (unsigned)d, (unsigned)h);
    else if (h > 0) snprintf(out, n, "%u ч %u м", (unsigned)h, (unsigned)((sec % 3600UL) / 60UL));
    else            snprintf(out, n, "%u мин", (unsigned)(sec / 60UL));
}

static void metrics_create(MetricsView *v, lv_obj_t *parent, const char *title, bool isServer) {
    memset(v, 0, sizeof(*v));
    v->isServer = isServer;

    v->scr = lv_obj_create(parent);
    ui_screen_style(v->scr);

    v->title = ui_label(v->scr, &lv_font_roboto_14, COL_TEXT_DIM, title);
    lv_obj_align(v->title, LV_ALIGN_TOP_MID, 0, 18);

    v->gauge = ring_gauge_create(v->scr, LCD_H_RES);
    lv_obj_center(v->gauge);

    if (isServer) {
        /* СЕРВЕР: CPU%, T CPU, RAM (GB), SSD T, Uptime.
         * Размеры шрифтов и отступ CPU↔T CPU — как на экране «ПК»
         * (roboto_40 + roboto_16, зазор 36 px между центрами), чтобы при
         * перелистывании строки не «прыгали».
         * Весь блок «CPU% + T CPU» сдвинут вверх на 8 px относительно ПК
         * (y=-52 / -16 вместо -44 / -8), чтобы зазор «заголовок → CPU%»
         * стал 8 px — ровно как между RAM и SSD температурой. Взаимный
         * зазор внутри блока при этом не меняется (-16 - (-52) = 36 px). */
        v->cpu       = ui_label(v->scr, &lv_font_roboto_40, COL_TEXT, "—");
        lv_obj_align(v->cpu, LV_ALIGN_CENTER, 0, -52);

        v->cpu_temp  = ui_label(v->scr, &lv_font_roboto_16, COL_TEMP, "—");
        lv_obj_align(v->cpu_temp, LV_ALIGN_CENTER, 0, -16);

        v->mem       = ui_label(v->scr, &lv_font_roboto_20, COL_TEXT, "—");
        lv_obj_align(v->mem, LV_ALIGN_CENTER, 0, 18);

        v->disk_temp = ui_label(v->scr, &lv_font_roboto_20, COL_TEMP, "—");
        lv_obj_align(v->disk_temp, LV_ALIGN_CENTER, 0, 50);

        v->up        = ui_label(v->scr, &lv_font_roboto_16, COL_TEXT_DIM, "—");
        lv_obj_align(v->up, LV_ALIGN_CENTER, 0, 80);

        v->gpu = v->gpu_temp = nullptr;
    } else {
        /* ПК: CPU% + T CPU (слева), GPU% + T GPU (справа), RAM (GB), SSD T */
        v->cpu       = ui_label(v->scr, &lv_font_roboto_40, COL_TEXT, "—");
        lv_obj_align(v->cpu, LV_ALIGN_CENTER, -50, -44);

        v->cpu_temp  = ui_label(v->scr, &lv_font_roboto_16, COL_TEMP, "—");
        lv_obj_align(v->cpu_temp, LV_ALIGN_CENTER, -50, -8);

        v->gpu       = ui_label(v->scr, &lv_font_roboto_40, COL_TEXT, "—");
        lv_obj_align(v->gpu, LV_ALIGN_CENTER, 50, -44);

        v->gpu_temp  = ui_label(v->scr, &lv_font_roboto_16, COL_TEMP, "—");
        lv_obj_align(v->gpu_temp, LV_ALIGN_CENTER, 50, -8);

        v->mem       = ui_label(v->scr, &lv_font_roboto_20, COL_TEXT, "—");
        lv_obj_align(v->mem, LV_ALIGN_CENTER, 0, 30);

        v->disk_temp = ui_label(v->scr, &lv_font_roboto_20, COL_TEMP, "—");
        lv_obj_align(v->disk_temp, LV_ALIGN_CENTER, 0, 64);

        v->up = nullptr;
    }
}

static void metrics_render(MetricsView *v, const DeviceMetrics &d) {
    char b[64];

    if (!d.valid) {
        lv_label_set_text(v->cpu, "—");
        if (v->cpu_temp)  lv_label_set_text(v->cpu_temp, "нет данных");
        if (v->gpu)       lv_label_set_text(v->gpu, "—");
        if (v->gpu_temp)  lv_label_set_text(v->gpu_temp, "—");
        if (v->mem)       lv_label_set_text(v->mem, "—");
        if (v->disk_temp) lv_label_set_text(v->disk_temp, "—");
        if (v->up)        lv_label_set_text(v->up, "—");
        ring_gauge_set(v->gauge, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 100.0f, RING_HUMIDITY);
        return;
    }

    /* CPU */
    snprintf(b, sizeof(b), "%.0f%%", (double)d.cpuPct);
    lv_label_set_text(v->cpu, b);

    /* CPU Temp */
    if (v->cpu_temp) {
        if (d.tempC > -900.0f) snprintf(b, sizeof(b), "%.0f°C", (double)d.tempC);
        else                  snprintf(b, sizeof(b), "—");
        lv_label_set_text(v->cpu_temp, b);
    }

    /* GPU (только для ПК) */
    if (v->gpu) {
        snprintf(b, sizeof(b), "%.0f%%", (double)d.gpuPct);
        lv_label_set_text(v->gpu, b);
    }
    if (v->gpu_temp) {
        if (d.gpuTempC > -900.0f) snprintf(b, sizeof(b), "%.0f°C", (double)d.gpuTempC);
        else                      snprintf(b, sizeof(b), "—");
        lv_label_set_text(v->gpu_temp, b);
    }

    /* RAM в GB */
    if (v->mem) {
        if (d.memTotalMb > 0) {
            const float usedGb  = d.memUsedMb / 1024.0f;
            const float totalGb = d.memTotalMb / 1024.0f;
            snprintf(b, sizeof(b), "RAM %.1f/%.1f ГБ", (double)usedGb, (double)totalGb);
        } else {
            snprintf(b, sizeof(b), "RAM —");
        }
        lv_label_set_text(v->mem, b);
    }

    /* Disk Temp */
    if (v->disk_temp) {
        if (d.diskTempC > -900.0f) snprintf(b, sizeof(b), "SSD %.0f°C", (double)d.diskTempC);
        else                       snprintf(b, sizeof(b), "SSD —");
        lv_label_set_text(v->disk_temp, b);
    }

    /* Uptime (только для сервера) */
    if (v->up) {
        char up[24];
        fmt_uptime(d.uptimeSec, up, sizeof(up));
        snprintf(b, sizeof(b), "аптайм %s", up);
        lv_label_set_text(v->up, b);
    }

    if (v->isServer) {
        /* Сервер: верх - CPU% (синий-бирюза-жёлтый-красный), низ - CPU temp (зелёный-жёлтый-оранжевый-красный) */
        ring_gauge_set(v->gauge, d.cpuPct, 0.0f, 100.0f, d.tempC, 0.0f, 100.0f, RING_SERVER);
    } else {
        /* ПК: верх - CPU temp, низ - GPU temp (оба зелёный-жёлтый-оранжевый-красный) */
        ring_gauge_set(v->gauge, d.tempC, 0.0f, 100.0f, d.gpuTempC, 0.0f, 100.0f, RING_TEMP);
    }
}

static void metrics_update_view(MetricsView *v) {
    const uint32_t sec = millis() / 1000;
    if ((int)sec == v->lastSec) return;
    v->lastSec = (int)sec;

    state_lock();
    const DeviceMetrics d = v->isServer ? g_state.server : g_state.pc;
    state_unlock();

    metrics_render(v, d);
}

/* ---------------- Экран «Сервер» ---------------- */

lv_obj_t *scr_server_create(lv_obj_t *parent) {
    metrics_create(&s_viewServer, parent, "Сервер", true);
    return s_viewServer.scr;
}

void scr_server_update(void) {
    metrics_update_view(&s_viewServer);
}

/* ---------------- Экран «ПК» ---------------- */

lv_obj_t *scr_pc_create(lv_obj_t *parent) {
    metrics_create(&s_viewPc, parent, "ПК", false);
    return s_viewPc.scr;
}

void scr_pc_update(void) {
    metrics_update_view(&s_viewPc);
}