#include "tempest_state.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

static TempestState s_state;
static SemaphoreHandle_t s_mutex = nullptr;

// 24-hour pressure history ring buffer (48 samples, 1 every 30 min)
#define PRESSURE_SAMPLES 48
static float s_pressure_hist[PRESSURE_SAMPLES];
static int   s_pressure_count = 0;
static int   s_pressure_idx = 0;
static uint32_t s_last_pressure_sample_ms = 0;

// Lightning strike timestamps for 3-hour window
#define STRIKE_BUF_SIZE 64
static int64_t s_strike_timestamps[STRIKE_BUF_SIZE];
static int     s_strike_count = 0;

void tempest_state_init() {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_state, 0, sizeof(s_state));
    s_state.units = DEFAULT_UNIT_SYSTEM;
    s_state.air_temp_c = 21.0f;
    s_state.humidity_pct = 50.0f;
    s_state.pressure_mb = 1013.25f;
    for (int i = 0; i < 48; ++i) {
        s_pressure_hist[i] = 1013.25f;
        s_state.pressure_hist_24h[i] = 1013.25f;
    }
    s_pressure_count = 48;
    s_state.pressure_hist_count = 48;
    strncpy(s_state.conditions_text, "Connecting...", sizeof(s_state.conditions_text));
    strncpy(s_state.icon_slug, "partly-cloudy-day", sizeof(s_state.icon_slug));
    s_state.lightning_dist_km = -1.0f; // negative means none
}

void tempest_get_state(TempestState *dest) {
    if (!dest) return;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(dest, &s_state, sizeof(TempestState));
        xSemaphoreGive(s_mutex);
    }
}

int tempest_get_pressure_history(float *dest_buf, int max_samples) {
    if (!dest_buf || max_samples <= 0) return 0;
    int count = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        count = s_state.pressure_hist_count;
        if (count > max_samples) count = max_samples;
        for (int i = 0; i < count; ++i) {
            dest_buf[i] = s_state.pressure_hist_24h[i];
        }
        xSemaphoreGive(s_mutex);
    }
    return count;
}

void tempest_update_obs(float temp_c, float humidity, float pressure,
                        float wind_avg, float wind_gust, float wind_lull, int wind_dir,
                        float uv, float solar, float rain_min,
                        float strike_dist, int strike_cnt, float battery, int64_t epoch) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    s_state.air_temp_c = temp_c;
    s_state.humidity_pct = humidity;
    s_state.pressure_mb = pressure;
    s_state.wind_avg_ms = wind_avg;
    s_state.wind_gust_ms = wind_gust;
    s_state.wind_lull_ms = wind_lull;
    s_state.wind_dir_deg = wind_dir;
    s_state.uv_index = uv;
    s_state.solar_wm2 = solar;
    s_state.rain_last_min_mm = rain_min;
    if (strike_dist > 0.0f) {
        s_state.lightning_dist_km = strike_dist;
    }
    s_state.lightning_count_today = strike_cnt;
    s_state.battery_v = battery;
    s_state.last_obs_epoch = epoch;
    s_state.last_packet_millis = millis();
    s_state.udp_connected = true;

    // Track 24-hour pressure history (48 samples, 1 every 30 mins)
    uint32_t now_ms = millis();
    if (s_state.pressure_hist_count == 0 || (now_ms - s_last_pressure_sample_ms >= 1800000UL)) {
        s_last_pressure_sample_ms = now_ms;
        if (s_state.pressure_hist_count == 0) {
            // Seed initial samples with current pressure so sparkline isn't empty
            for (int i = 0; i < 48; ++i) s_pressure_hist[i] = pressure;
            s_pressure_count = 48;
            s_pressure_idx = 0;
        } else {
            s_pressure_hist[s_pressure_idx] = pressure;
            s_pressure_idx = (s_pressure_idx + 1) % 48;
            if (s_pressure_count < 48) s_pressure_count++;
        }

        // Copy in chronological order (oldest to newest)
        s_state.pressure_hist_count = s_pressure_count;
        int start = (s_pressure_count < 48) ? 0 : s_pressure_idx;
        for (int i = 0; i < s_pressure_count; ++i) {
            s_state.pressure_hist_24h[i] = s_pressure_hist[(start + i) % 48];
        }

        // 3-hour delta (6 samples ago)
        int delta_idx = (s_pressure_count >= 6) ? (s_pressure_count - 6) : 0;
        s_state.pressure_trend_mb = pressure - s_state.pressure_hist_24h[delta_idx];
    }

    xSemaphoreGive(s_mutex);
}

void tempest_update_rapid_wind(float speed_ms, int dir_deg, int64_t epoch) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_state.rapid_wind_ms = speed_ms;
    s_state.rapid_wind_dir = dir_deg;
    s_state.rapid_wind_epoch = epoch;
    s_state.last_packet_millis = millis();
    s_state.udp_connected = true;
    xSemaphoreGive(s_mutex);
}

void tempest_update_strike(float dist_km, uint32_t energy, int64_t epoch) {
    (void)energy;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_state.lightning_dist_km = dist_km;
    s_state.last_strike_epoch = epoch;
    s_state.strike_alert_active = true;

    // Add to 3-hour strike counter
    if (s_strike_count < STRIKE_BUF_SIZE) {
        s_strike_timestamps[s_strike_count++] = epoch;
    } else {
        memmove(&s_strike_timestamps[0], &s_strike_timestamps[1], (STRIKE_BUF_SIZE - 1) * sizeof(int64_t));
        s_strike_timestamps[STRIKE_BUF_SIZE - 1] = epoch;
    }

    // Prune strikes older than 3 hours (10800 seconds)
    int valid = 0;
    for (int i = 0; i < s_strike_count; ++i) {
        if (epoch - s_strike_timestamps[i] <= 10800LL) {
            valid++;
        }
    }
    s_state.lightning_count_3h = valid;

    xSemaphoreGive(s_mutex);
}

void tempest_clear_strike_alert() {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_state.strike_alert_active = false;
    xSemaphoreGive(s_mutex);
}

void tempest_update_forecast(const char *conditions, const char *icon,
                            float high_c, float low_c, float feels_c) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (conditions && strlen(conditions) > 0) {
        strncpy(s_state.conditions_text, conditions, sizeof(s_state.conditions_text) - 1);
        s_state.conditions_text[sizeof(s_state.conditions_text) - 1] = '\0';
    }
    if (icon && strlen(icon) > 0) {
        strncpy(s_state.icon_slug, icon, sizeof(s_state.icon_slug) - 1);
        s_state.icon_slug[sizeof(s_state.icon_slug) - 1] = '\0';
    }
    s_state.temp_high_c = high_c;
    s_state.temp_low_c = low_c;
    s_state.feels_like_c = feels_c;
    s_state.rest_connected = true;
    xSemaphoreGive(s_mutex);
}

void tempest_set_units(UnitSystem u) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_state.units = u;
    xSemaphoreGive(s_mutex);
}

UnitSystem tempest_get_units() {
    UnitSystem u = DEFAULT_UNIT_SYSTEM;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        u = s_state.units;
        xSemaphoreGive(s_mutex);
    }
    return u;
}

float temp_to_unit(float temp_c, UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? (temp_c * 1.8f + 32.0f) : temp_c;
}

float wind_to_unit(float wind_ms, UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? (wind_ms * 2.23694f) : wind_ms;
}

float dist_to_unit(float dist_km, UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? (dist_km * 0.621371f) : dist_km;
}

float pressure_to_unit(float pressure_mb, UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? (pressure_mb * 0.02953f) : pressure_mb;
}

const char* temp_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "°F" : "°C";
}

const char* wind_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "mph" : "m/s";
}

const char* dist_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "mi" : "km";
}

const char* pressure_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "inHg" : "mb";
}

const char* wind_cardinal(int degrees) {
    static const char *cardinals[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int idx = (int)((degrees + 11.25f) / 22.5f) & 15;
    return cardinals[idx];
}
