#include "brightness_mgr.h"
#include "config.h"
#include "weather_policy.h"
#include "display.h"
#include <Arduino.h>
#include <time.h>

static bool s_preview = false;
static uint8_t s_preview_level;
static uint32_t s_preview_started;
static int s_applied = -1;
static uint32_t s_previous_idle = 0;
static bool s_touched = false;

void brightness_preview(uint8_t level) {
    s_preview = true;
    s_preview_level = level;
    s_preview_started = millis();
}
void brightness_cancel_preview() { s_preview = false; }

void brightness_tick(const AppSettings &s, uint32_t idle_ms) {
    // LVGL inactivity drops when the touch input is active. Boot alone must not
    // wake an otherwise dark night display; held touches keep inactivity low.
    if (idle_ms < s_previous_idle) s_touched = true;
    s_previous_idle = idle_ms;
    if (s_preview && uint32_t(millis() - s_preview_started) >= 5000) s_preview = false;
    bool idle = s.dim_timeout_s && idle_ms >= uint32_t(s.dim_timeout_s) * 1000UL;
    bool night = false;
    time_t now = time(nullptr);
    if (s.night_mode_enabled && now > 1000000000LL) {
        struct tm ti;
        localtime_r(&now, &ti);
        night = weather_night_hour(ti.tm_hour, s.night_start_hour, s.night_end_hour);
    }
    uint8_t target = s_preview ? s_preview_level : weather_brightness(idle, night, s.brightness_day,
                                                                    s.brightness_dim, s.brightness_night);
    if (!s_preview && night && s_touched && s.night_wake_s &&
        idle_ms < uint32_t(s.night_wake_s) * 1000UL) {
        // A user-selected zero idle brightness still needs a usable wake level.
        uint8_t wake = s.brightness_dim ? s.brightness_dim : BRIGHTNESS_DIM;
        if (wake > target) target = wake;
    }
    if (target != s_applied) { display::setBrightness(target); s_applied = target; }
}
