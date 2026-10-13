#include "time_sync.h"
#include "app_config.h"

#include <Arduino.h>
#include <time.h>

void time_begin() {
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

bool time_is_valid() {
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return (tm_now.tm_year + 1900) > 2024;
}
