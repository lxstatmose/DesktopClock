#include <Arduino.h>
#include <lvgl.h>

#include "app_config.h"
#include "app_state.h"

#include "display/display.h"
#include "input/button.h"
#include "net/net_task.h"
#include "net/wifi_manager.h"
#include "ui/ui_manager.h"

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    /* Дефолтный TX-буфер USB-CDC — всего 256 Б. При порции логов на старте
     * (Wi-Fi, скан I2C) он забивается, и HWCDC::write при tx_timeout_ms=0
     * молча отбрасывает остаток строки — в мониторе появляется мусор вида
     * "[air] сканируюину". 4 КБ всегда вмещают burst логов.
     * Таймаут 20 мс: если кольцо всё же заполнилось (хост не читает лог),
     * write подождёт, пока ISR выгрузит данные в FIFO, вместо потери байтов.
     * Логи пишет сетевая задача на ядре 0, UI это не задевает. */
    Serial.setTxBufferSize(4096);
    Serial.setTxTimeoutMs(20);
#endif
    delay(200);
    Serial.println("\n=== DesktopClock: ESP32-S3 + GC9A01 + LVGL 9 ===");

    /* Снизить частоту CPU: 240 МГц по умолчанию даёт максимум нагрев,
     * LVGL/сеть на 160 МГц работают без заметной потери плавности. */
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.printf("[main] CPU: %d МГц\n", getCpuFrequencyMhz());

    state_init();      /* мьютекс и обнуление состояния */
    display_init();    /* LovyanGFX + LVGL */
    ui_init();         /* экраны и первый показ */

    button_begin();    /* TTP223 */
    wifi_begin();      /* старт подключения к WiFi */

    net_task_start();  /* сеть/сенсоры на ядре 0 */

    Serial.println("[main] готово");
}

void loop() {
    /* Отрисовка LVGL и опрос кнопки — только на ядре 1 (LVGL не потокобезопасен) */
    lv_timer_handler();

    const ButtonEvent ev = button_poll();
    if (ev == BTN_CLICK) {
        ui_next();
    } else if (ev == BTN_LONG_PRESS) {
        ui_prev();
    }

    /* Обновление данных на активном экране — 5 раз в секунду */
    static uint32_t lastUi = 0;
    if (millis() - lastUi >= 200) {
        lastUi = millis();
        ui_update();
    }

    delay(UI_TICK_MS);
}
