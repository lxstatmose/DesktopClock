#pragma once
/*
 * lgfx_config.hpp — конфигурация LovyanGFX для круглого GC9A01 (240x240)
 * на ESP32-S3 SuperMini. Класс LGFX используется драйвером дисплея LVGL.
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "pin_config.h"
#include "app_config.h"     /* нужен для LCD_H_RES / LCD_V_RES */

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;
    lgfx::Light_PWM    _light_instance;

public:
    LGFX() {
        /* ---------- SPI-шина ---------- */
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = SPI2_HOST;      /* ESP32-S3: SPI2 = FSPI */
            cfg.spi_mode    = 0;
            cfg.freq_write  = 80000000;       /* 80 МГц для плавной отрисовки */
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_LCD_SCLK;
            cfg.pin_mosi    = PIN_LCD_MOSI;
            cfg.pin_miso    = -1;             /* не используется (только запись) */
            cfg.pin_dc      = PIN_LCD_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        /* ---------- Панель GC9A01 ---------- */
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = PIN_LCD_CS;
            cfg.pin_rst          = PIN_LCD_RST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = LCD_H_RES; /* 240 */
            cfg.panel_height     = LCD_V_RES; /* 240 */
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;
            cfg.invert           = true;      /* у GC9A01 обычно нужна инверсия */
            cfg.rgb_order        = false;     /* RGB; если цвета поменяются местами -> true */
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }

        /* ---------- Подсветка (PWM на BL) ---------- */
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_LCD_BL;
            cfg.invert      = !LCD_BL_ACTIVE_HIGH;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

/* Единственный экземпляр дисплея (определён в display.cpp) */
extern LGFX lcd;
