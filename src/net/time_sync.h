#pragma once
/*
 * time_sync.h — синхронизация времени по NTP.
 */

void time_begin();       /* настроить TZ + запустить SNTP */
bool time_is_valid();    /* время получено (год > 2024) */
