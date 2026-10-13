#pragma once
/*
 * net_task.h — фоновая задача (ядро 0): WiFi, NTP,
 * погода и датчики. UI живёт на ядре 1 и не блокируется сетью.
 */

void net_task_start();
