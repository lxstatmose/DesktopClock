#pragma once
/*
 * button.h — обработка кнопки TTP223 (или обычной тактильной).
 * Короткое нажатие / длинное нажатие.
 */

enum ButtonEvent {
    BTN_NONE = 0,
    BTN_CLICK,
    BTN_LONG_PRESS
};

void        button_begin();
ButtonEvent button_poll();   /* вызывать часто (в loop) */
