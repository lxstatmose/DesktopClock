#pragma once
/*
 * wifi_manager.h — подключение/переподключение к WiFi.
 */

void wifi_begin();           /* запустить подключение */
bool wifi_is_connected();
void wifi_loop();            /* периодически вызывать для авто-переподключения */
void wifi_log_scan();        /* разовый скан и печать видимых сетей (для отладки) */
