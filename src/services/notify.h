#pragma once
/*
 * notify.h — уведомления с часов на телефон через ntfy.sh.
 *
 * Основной канал — ntfy.sh (обычный HTTPS, этот домен не режется DPI):
 * приложение ntfy на Android/iOS подписывается на топик и показывает
 * уведомления.
 *
 * Настройки (топик) — в app_config.h.
 */

bool notify_send(const char *text);

bool notify_send_weather(const char *weatherText);  // formatted by caller

bool notify_enabled(void);

void notify_check_alerts(void);