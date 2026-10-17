#pragma once
#include <stddef.h>
#include <stdio.h>

inline void weather_format_clock(char *out, size_t size, int hour, int minute, int second,
                                 bool clock_24h, bool show_seconds = false) {
    if (clock_24h) {
        if (show_seconds) snprintf(out, size, "%02d:%02d:%02d", hour, minute, second);
        else snprintf(out, size, "%02d:%02d", hour, minute);
    } else {
        int h = hour % 12;
        if (!h) h = 12;
        if (show_seconds) snprintf(out, size, "%d:%02d:%02d %s", h, minute, second, hour < 12 ? "AM" : "PM");
        else snprintf(out, size, "%d:%02d %s", h, minute, hour < 12 ? "AM" : "PM");
    }
}
