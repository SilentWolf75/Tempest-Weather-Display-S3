#pragma once
#include <stdint.h>

// Shared by firmware and host regression checks. Unsigned subtraction handles rollover.
inline bool weather_recent(bool received, uint32_t now, uint32_t then, uint32_t limit) {
    return received && uint32_t(now - then) < limit;
}

inline bool weather_night_hour(int hour, int start, int end) {
    if (start == end) return false;
    return start > end ? (hour >= start || hour < end) : (hour >= start && hour < end);
}

inline uint8_t weather_brightness(bool idle, bool night, uint8_t day, uint8_t dim, uint8_t bedtime) {
    return night ? bedtime : (idle ? dim : day);
}

// Info is always a manual page; empty selection holds the current weather page.
inline int weather_next_screen(int current, uint8_t mask) {
    if (current < 0 || current >= 4) return current;
    for (int step = 1; step <= 4; ++step) {
        int page = (current + step) % 4;
        if (mask & (1 << page)) return page;
    }
    return current;
}
