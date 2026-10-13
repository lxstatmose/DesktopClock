#include "button.h"

#include "pin_config.h"
#include "app_config.h"
#include <Arduino.h>

static bool     s_raw        = false;
static bool     s_stable     = false;
static uint32_t s_lastChange = 0;
static uint32_t s_pressStart = 0;
static bool     s_longSent   = false;

static inline bool readPressed() {
#if BUTTON_ACTIVE_LOW
    return digitalRead(PIN_BUTTON) == LOW;
#else
    return digitalRead(PIN_BUTTON) == HIGH;
#endif
}

void button_begin() {
#if BUTTON_USE_PULLUP
    pinMode(PIN_BUTTON, INPUT_PULLUP);
#else
    pinMode(PIN_BUTTON, INPUT);
#endif
    s_raw = s_stable = readPressed();
    s_lastChange = millis();
}

ButtonEvent button_poll() {
    const bool now = readPressed();

    /* антидребезг */
    if (now != s_raw) {
        s_raw = now;
        s_lastChange = millis();
    }

    if ((millis() - s_lastChange) >= BTN_DEBOUNCE_MS && s_raw != s_stable) {
        s_stable = s_raw;
        if (s_stable) {                 /* нажали */
            s_pressStart = millis();
            s_longSent   = false;
        } else {                        /* отпустили */
            if (!s_longSent) return BTN_CLICK;
        }
    }

    /* длинное нажатие — срабатывает один раз, не дожидаясь отпускания */
    if (s_stable && !s_longSent && (millis() - s_pressStart) >= BTN_LONGPRESS_MS) {
        s_longSent = true;
        return BTN_LONG_PRESS;
    }

    return BTN_NONE;
}
