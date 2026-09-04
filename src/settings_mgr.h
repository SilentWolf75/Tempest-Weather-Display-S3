#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum TzPreset {
    TZ_EASTERN = 0,
    TZ_CENTRAL = 1,
    TZ_MOUNTAIN = 2,
    TZ_ARIZONA = 3,
    TZ_PACIFIC = 4,
    TZ_ALASKA = 5,
    TZ_HAWAII = 6,
    TZ_UTC = 7,
    TZ_CUSTOM = 8
};

struct AppSettings {
    char     wifi_ssid[33];     // Wi-Fi SSID
    char     wifi_password[65]; // Wi-Fi Password
    uint8_t  tz_preset;         // TzPreset enum (0..8)
    char     tz_custom[64];     // Custom POSIX timezone string
    char     zipcode[10];       // 5-digit US zipcode
    uint32_t station_id;        // WeatherFlow Tempest station ID
    char     api_token[128];    // WeatherFlow Personal Access Token
    uint8_t  units;             // 0 = Imperial (°F, mph, inHg, in, mi), 1 = Metric (°C, m/s, mb, mm, km)
    uint8_t  brightness_day;    // 10..255 (default ~210)
    uint8_t  brightness_dim;    // 5..100  (default ~35)
    uint16_t dim_timeout_s;     // Idle seconds before dimming (0 = never dim, default 45)
    uint16_t auto_scroll_s;     // Seconds between auto screen scroll (0 = disabled, default 10)
    uint16_t screen_rotation;   // 0, 90, 180, 270 (default 270)
};

void settings_init();
void settings_get(AppSettings *out);
void settings_save(const AppSettings *in);
const char* settings_get_tz_posix();
const char* settings_get_tz_name(uint8_t preset);
void settings_apply_timezone();
