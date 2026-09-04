#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

struct TempestState {
    // Live observations (SI units internally)
    float    air_temp_c;
    float    humidity_pct;
    float    pressure_mb;
    float    pressure_trend_mb;    // 3-hour pressure delta
    float    wind_avg_ms;
    float    wind_gust_ms;
    float    wind_lull_ms;
    int      wind_dir_deg;

    // Rapid wind (updated every ~3 seconds from UDP)
    float    rapid_wind_ms;
    int      rapid_wind_dir;
    int64_t  rapid_wind_epoch;

    // Solar & rain
    float    uv_index;
    float    solar_wm2;
    float    rain_last_min_mm;
    float    rain_today_mm;

    // Lightning metrics
    float    lightning_dist_km;
    int      lightning_count_today;
    int      lightning_count_3h;
    int64_t  last_strike_epoch;
    bool     strike_alert_active;  // triggered on evt_strike

    // Battery & connection status
    float    battery_v;
    int64_t  last_obs_epoch;
    uint32_t last_packet_millis;
    bool     udp_connected;

    // REST Forecast data
    char     conditions_text[48];
    char     icon_slug[32];
    float    temp_high_c;
    float    temp_low_c;
    float    feels_like_c;
    bool     rest_connected;

    // User settings
    UnitSystem units;
};

void tempest_state_init();
void tempest_get_state(TempestState *dest);

void tempest_update_obs(float temp_c, float humidity, float pressure,
                        float wind_avg, float wind_gust, float wind_lull, int wind_dir,
                        float uv, float solar, float rain_min,
                        float strike_dist, int strike_cnt, float battery, int64_t epoch);

void tempest_update_rapid_wind(float speed_ms, int dir_deg, int64_t epoch);
void tempest_update_strike(float dist_km, uint32_t energy, int64_t epoch);
void tempest_clear_strike_alert();

void tempest_update_forecast(const char *conditions, const char *icon,
                            float high_c, float low_c, float feels_c);

void tempest_set_units(UnitSystem u);
UnitSystem tempest_get_units();

// Helper conversion functions
float temp_to_unit(float temp_c, UnitSystem u);
float wind_to_unit(float wind_ms, UnitSystem u);
float dist_to_unit(float dist_km, UnitSystem u);
float pressure_to_unit(float pressure_mb, UnitSystem u);

const char* temp_unit_str(UnitSystem u);
const char* wind_unit_str(UnitSystem u);
const char* dist_unit_str(UnitSystem u);
const char* pressure_unit_str(UnitSystem u);
const char* wind_cardinal(int degrees);
