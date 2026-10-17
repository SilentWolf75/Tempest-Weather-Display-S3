#include "tempest_state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "weather_policy.h"
#include "weather_ledger.h"

#pragma pack(push, 1)
struct WeatherHistoryBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t pressure_count;
    uint16_t pressure_idx;
    uint16_t reserved;
    float pressure[48];
    int64_t pressure_epoch[48];
    int64_t last_obs;
    int64_t last_strike;
    int32_t day;
    float today;
    float yesterday;
    int32_t strikes_today;
    uint8_t complete;
    uint8_t pad[3];
    struct {
        int64_t minute;
        int64_t report_epoch;
        int32_t count;
        float rain;
        float closest;
    } buckets[181];
};
#pragma pack(pop)
static_assert(sizeof(WeatherHistoryBlob) < 6000, "history blob must stay a small NVS record");

static TempestState s_state;
static SemaphoreHandle_t s_mutex = nullptr;

// 24-hour pressure history ring buffer (48 samples, 1 every 30 min)
#define PRESSURE_SAMPLES 48
static float s_pressure_hist[PRESSURE_SAMPLES];
static int   s_pressure_count = 0;
static int   s_pressure_idx = 0;
static uint32_t s_last_pressure_sample_ms = 0;
static int64_t s_pressure_epochs[PRESSURE_SAMPLES];
static WeatherLedger s_ledger;
static int64_t s_last_udp_obs_epoch = 0;
static int64_t s_last_cloud_obs_epoch = 0;
static bool s_packet_received = false;

static const uint32_t HISTORY_MAGIC = 0x31485857u;
static void refresh_ledger(int64_t epoch);

static void history_store() {
    WeatherHistoryBlob *blob = (WeatherHistoryBlob *)malloc(sizeof(WeatherHistoryBlob));
    if (!blob || !s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        free(blob);
        return;
    }
    memset(blob, 0, sizeof(*blob));
    blob->magic = HISTORY_MAGIC;
    blob->version = 1;
    blob->pressure_count = (uint16_t)s_pressure_count;
    blob->pressure_idx = (uint16_t)s_pressure_idx;
    memcpy(blob->pressure, s_pressure_hist, sizeof(blob->pressure));
    memcpy(blob->pressure_epoch, s_pressure_epochs, sizeof(blob->pressure_epoch));
    blob->last_obs = s_ledger.last_obs;
    blob->last_strike = s_ledger.last_strike;
    blob->day = s_ledger.day;
    blob->today = s_ledger.today;
    blob->yesterday = s_ledger.yesterday;
    blob->strikes_today = s_ledger.strikes_today;
    blob->complete = s_ledger.complete ? 1 : 0;
    for (int i = 0; i < 181; ++i) {
        blob->buckets[i].minute = s_ledger.buckets[i].minute;
        blob->buckets[i].report_epoch = s_ledger.buckets[i].report_epoch;
        blob->buckets[i].count = s_ledger.buckets[i].count;
        blob->buckets[i].rain = s_ledger.buckets[i].rain;
        blob->buckets[i].closest = s_ledger.buckets[i].closest;
    }
    xSemaphoreGive(s_mutex);
    Preferences prefs;
    bool ok = prefs.begin("wxhist", false) && prefs.putBytes("hist", blob, sizeof(*blob)) == sizeof(*blob);
    if (!ok) Serial.println("[history] NVS write failed");
    free(blob);
}

static bool history_load_locked() {
    Preferences prefs;
    if (!prefs.begin("wxhist", true)) return false;
    WeatherHistoryBlob *blob = (WeatherHistoryBlob *)malloc(sizeof(WeatherHistoryBlob));
    if (!blob) return false;
    size_t n = prefs.getBytes("hist", blob, sizeof(*blob));
    bool ok = n == sizeof(*blob) && blob->magic == HISTORY_MAGIC && blob->version == 1 &&
              blob->pressure_count <= PRESSURE_SAMPLES && blob->pressure_idx < PRESSURE_SAMPLES;
    bool rolled = false;
    if (ok) {
        s_pressure_count = blob->pressure_count;
        s_pressure_idx = blob->pressure_idx;
        memcpy(s_pressure_hist, blob->pressure, sizeof(s_pressure_hist));
        memcpy(s_pressure_epochs, blob->pressure_epoch, sizeof(s_pressure_epochs));
        s_ledger.last_obs = blob->last_obs;
        s_ledger.last_strike = blob->last_strike;
        s_ledger.day = blob->day;
        s_ledger.today = blob->today;
        s_ledger.yesterday = blob->yesterday;
        s_ledger.strikes_today = blob->strikes_today;
        s_ledger.complete = blob->complete != 0;
        for (int i = 0; i < 181; ++i) {
            s_ledger.buckets[i].minute = blob->buckets[i].minute;
            s_ledger.buckets[i].report_epoch = blob->buckets[i].report_epoch;
            s_ledger.buckets[i].count = blob->buckets[i].count;
            s_ledger.buckets[i].rain = blob->buckets[i].rain;
            s_ledger.buckets[i].closest = blob->buckets[i].closest;
        }
        s_state.pressure_hist_count = s_pressure_count;
        int start = (s_pressure_count < PRESSURE_SAMPLES) ? 0 : s_pressure_idx;
        for (int i = 0; i < s_pressure_count; ++i) {
            s_state.pressure_hist_24h[i] = s_pressure_hist[(start + i) % PRESSURE_SAMPLES];
            s_state.pressure_hist_epoch[i] = s_pressure_epochs[(start + i) % PRESSURE_SAMPLES];
        }
        int loaded_day = s_ledger.day;
        int64_t epoch = time(nullptr);
        if (epoch <= 1000000000LL) epoch = s_ledger.last_obs;
        refresh_ledger(epoch);
        rolled = loaded_day >= 0 && s_ledger.day != loaded_day;
    }
    free(blob);
    return rolled;
}

void tempest_cloud_attempt() {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.cloud_attempted = s_state.cloud_busy = true;
    s_state.cloud_attempt_millis = millis();
    xSemaphoreGive(s_mutex);
}

void tempest_cloud_finish(CloudResult result, int http_code, uint32_t retry_interval, bool service_cooldown) {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.cloud_busy = false;
    s_state.cloud_result = result;
    s_state.cloud_http_code = http_code;
    s_state.cloud_retry_started = millis();
    s_state.cloud_retry_interval = retry_interval;
    s_state.cloud_service_cooldown = service_cooldown;
    xSemaphoreGive(s_mutex);
}

static int local_day(int64_t epoch) {
    time_t t = (time_t)epoch;
    struct tm ti;
    localtime_r(&t, &ti);
    // A contiguous local calendar-day key, including leap years and year changes.
    int y = ti.tm_year + 1900;
    int prior = y - 1;
    return prior * 365 + prior / 4 - prior / 100 + prior / 400 + ti.tm_yday;
}

static void refresh_ledger(int64_t epoch) {
    if (epoch > 1000000000LL) s_ledger.rollover(local_day(epoch));
    s_state.rain_today_mm = s_ledger.today;
    s_state.rain_yesterday_mm = s_ledger.yesterday;
    s_state.rain_today_complete = s_ledger.complete;
    s_state.lightning_count_today = s_ledger.strikes_today;
    s_state.lightning_count_3h = s_ledger.count(epoch);
    s_state.closest_strike_km = s_ledger.closest(epoch);
    int valid = 0;
    for (int i = 0; i < s_state.recent_strike_count; ++i) {
        if (epoch >= s_state.recent_strikes[i].epoch && epoch - s_state.recent_strikes[i].epoch < 10800)
            s_state.recent_strikes[valid++] = s_state.recent_strikes[i];
    }
    s_state.recent_strike_count = valid;
}

void tempest_state_tick() {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    uint32_t now = millis();
    s_state.udp_connected = weather_recent(s_packet_received, now, s_state.last_packet_millis, 90000);
    s_state.rest_connected = weather_recent(s_state.has_forecast, now, s_state.last_forecast_millis, 1200000);
    int64_t epoch = time(nullptr);
    if (epoch <= 1000000000LL && s_state.has_observation)
        epoch = s_state.last_obs_epoch + uint32_t(now - s_state.last_obs_millis) / 1000;
    int day_before = s_ledger.day;
    refresh_ledger(epoch);
    bool rolled = day_before >= 0 && s_ledger.day != day_before;
    if (s_state.peak_gust_epoch && epoch - s_state.peak_gust_epoch > 1800) {
        s_state.peak_gust_ms = 0;
        s_state.peak_gust_epoch = 0;
    }
    xSemaphoreGive(s_mutex);
    if (rolled) history_store();
}

const char *tempest_feed_label(const TempestState &state) {
    if (!state.has_observation) return "Waiting for weather";
    uint32_t limit = state.observation_from_udp ? 90000UL : 1200000UL;
    if (!weather_recent(true, millis(), state.last_obs_millis, limit)) return "Stale weather data";
    time_t now = time(nullptr);
    if (now > 1000000000LL && now - state.last_obs_epoch > (state.observation_from_udp ? 90 : 1200))
        return "Stale weather data";
    return state.observation_from_udp ? "Live sensor data" : "Cloud weather data";
}

void tempest_state_init() {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!s_mutex) abort();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(&s_state, 0, sizeof(s_state));
    s_state.units = DEFAULT_UNIT_SYSTEM;
    s_state.air_temp_c = NAN;
    s_state.humidity_pct = NAN;
    s_state.pressure_mb = NAN;
    s_state.temp_high_c = s_state.temp_low_c = s_state.feels_like_c = NAN;
    s_state.wind_avg_ms = s_state.wind_gust_ms = s_state.wind_lull_ms = NAN;
    s_state.uv_index = s_state.solar_wm2 = NAN;
    s_state.battery_v = NAN;
    s_state.rain_rate_mm_hr = NAN;
    s_state.rain_yesterday_mm = NAN;
    s_state.rain_probability_pct = -1;
    s_state.dew_point_c = NAN;
    s_state.closest_strike_km = -1;
    s_ledger = WeatherLedger{};
    s_pressure_count = s_pressure_idx = 0;
    s_last_pressure_sample_ms = 0;
    s_last_udp_obs_epoch = s_last_cloud_obs_epoch = 0;
    s_packet_received = false;
    strncpy(s_state.conditions_text, "Forecast unavailable", sizeof(s_state.conditions_text));
    strncpy(s_state.icon_slug, "partly-cloudy-day", sizeof(s_state.icon_slug));
    s_state.lightning_dist_km = -1.0f; // negative means none
    bool rolled = history_load_locked();
    xSemaphoreGive(s_mutex);
    if (rolled) history_store();
}

void tempest_get_state(TempestState *dest) {
    if (!dest) return;
    memset(dest, 0, sizeof(*dest));
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
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
                        float uv, float solar, float rain_min, int precip_type,
                        float strike_dist, int strike_cnt, float battery, int64_t epoch, bool from_udp) {
    bool gust_ok = isfinite(wind_gust) && wind_gust >= 0;
    bool lull_ok = isfinite(wind_lull) && wind_lull >= 0;
    bool dir_ok = wind_dir >= 0 && wind_dir <= 360;
    if (from_udp) {
        if (!gust_ok || !lull_ok || !dir_ok) return;
    } else if ((!isnan(wind_gust) && !gust_ok) || (!isnan(wind_lull) && !lull_ok) || (wind_dir != -1 && !dir_ok)) {
        return;
    }
    if (epoch <= 1000000000LL || !isfinite(temp_c) || !isfinite(humidity) || humidity < 0 || humidity > 100 ||
        !isfinite(pressure) || pressure <= 0 || !isfinite(wind_avg) || wind_avg < 0 ||
        !isfinite(rain_min) || rain_min < 0 || strike_cnt < 0) return;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    int day_before = s_ledger.day;
    int64_t &last_epoch = from_udp ? s_last_udp_obs_epoch : s_last_cloud_obs_epoch;
    if (epoch <= last_epoch) { xSemaphoreGive(s_mutex); return; }
    last_epoch = epoch;
    if (from_udp) {
        s_ledger.observation(epoch, local_day(epoch), rain_min, strike_cnt);
        s_state.has_udp_observation = true;
        s_state.last_udp_obs_millis = millis();
        s_packet_received = true;
        s_state.last_packet_millis = millis();
        s_state.udp_connected = true;
    } else if (weather_recent(s_state.has_udp_observation, millis(), s_state.last_udp_obs_millis, 90000)) {
        xSemaphoreGive(s_mutex);
        return;
    }
    // An older cloud snapshot must never overwrite a newer sensor observation.
    if (s_state.has_observation && epoch < s_state.last_obs_epoch) { xSemaphoreGive(s_mutex); return; }
    s_state.has_observation = true;
    s_state.observation_from_udp = from_udp;
    s_state.last_obs_millis = millis();

    s_state.air_temp_c = temp_c;
    s_state.humidity_pct = humidity;
    s_state.pressure_mb = pressure;

    // Calculate Dew Point using Magnus-Tetens approximation
    if (humidity > 0.0f && humidity <= 100.0f) {
        float a = 17.27f, b = 237.7f;
        float alpha = ((a * temp_c) / (b + temp_c)) + logf(humidity / 100.0f);
        s_state.dew_point_c = (b * alpha) / (a - alpha);
    } else {
        s_state.dew_point_c = NAN;
    }

    s_state.wind_avg_ms = wind_avg;
    s_state.wind_gust_ms = wind_gust;
    s_state.wind_lull_ms = wind_lull;
    s_state.wind_dir_deg = wind_dir;

    // A missing cloud gust must not become a zero peak.
    if (gust_ok) {
        if (s_state.peak_gust_epoch > 0 && (epoch - s_state.peak_gust_epoch) > 1800LL) {
            s_state.peak_gust_ms = wind_gust;
            s_state.peak_gust_dir = wind_dir;
            s_state.peak_gust_epoch = epoch;
        } else if (wind_gust >= s_state.peak_gust_ms || s_state.peak_gust_epoch == 0) {
            s_state.peak_gust_ms = wind_gust;
            s_state.peak_gust_dir = wind_dir;
            s_state.peak_gust_epoch = epoch;
        }
    }

    s_state.uv_index = uv;
    s_state.solar_wm2 = solar;
    s_state.rain_last_min_mm = from_udp ? rain_min : NAN;
    s_state.rain_rate_mm_hr = from_udp ? rain_min * 60.0f : NAN;
    s_state.precip_type = from_udp ? precip_type : 0;

    if (rain_min > 0.0f) {
        s_state.is_raining = true;
        s_state.last_rain_epoch = epoch;
    } else {
        if (s_state.last_rain_epoch > 0 && (epoch - s_state.last_rain_epoch) > 180) {
            s_state.is_raining = false;
        }
    }

    // obs_st distance is an interval average, not an event or closest distance.
    (void)strike_dist;
    if (from_udp) {
        s_state.battery_v = isfinite(battery) && battery > 0 ? battery : NAN;
        s_state.has_battery = true;
        s_state.battery_epoch = epoch;
        s_state.battery_millis = millis();
    }
    s_state.last_obs_epoch = epoch;
    refresh_ledger(epoch);

    // Track 24-hour pressure history (48 samples, 1 every 30 mins of observation time).
    bool have_sample = s_pressure_count > 0;
    int64_t previous_epoch = have_sample ? s_pressure_epochs[(s_pressure_idx + PRESSURE_SAMPLES - 1) % PRESSURE_SAMPLES] : 0;
    if (have_sample && epoch > previous_epoch && epoch - previous_epoch > 3600) {
        s_pressure_count = s_pressure_idx = 0;
        s_state.pressure_hist_count = 0;
        s_state.pressure_trend_valid = false;
        have_sample = false;
    }
    bool sampled = !have_sample || (epoch >= previous_epoch && epoch - previous_epoch >= 1800);
    if (sampled) {
        s_last_pressure_sample_ms = millis();
        s_pressure_hist[s_pressure_idx] = pressure;
        s_pressure_epochs[s_pressure_idx] = epoch;
        s_pressure_idx = (s_pressure_idx + 1) % PRESSURE_SAMPLES;
        if (s_pressure_count < PRESSURE_SAMPLES) s_pressure_count++;

        // Copy in chronological order (oldest to newest)
        s_state.pressure_hist_count = s_pressure_count;
        int start = (s_pressure_count < 48) ? 0 : s_pressure_idx;
        for (int i = 0; i < s_pressure_count; ++i) {
            s_state.pressure_hist_24h[i] = s_pressure_hist[(start + i) % 48];
            s_state.pressure_hist_epoch[i] = s_pressure_epochs[(start + i) % 48];
        }

        // 3-hour delta (6 samples ago)
        s_state.pressure_trend_valid = false;
        for (int i = s_pressure_count - 1; i >= 0; --i) {
            int idx = (start + i) % PRESSURE_SAMPLES;
            if (epoch - s_pressure_epochs[idx] >= 10800) {
                s_state.pressure_trend_mb = pressure - s_pressure_hist[idx];
                s_state.pressure_trend_valid = true;
                break;
            }
        }
    }

    bool rolled = day_before >= 0 && s_ledger.day != day_before;
    xSemaphoreGive(s_mutex);
    if (sampled || rolled) history_store();
}

void tempest_update_rapid_wind(float speed_ms, int dir_deg, int64_t epoch) {
    if (!isfinite(speed_ms) || speed_ms < 0 || dir_deg < 0 || dir_deg > 360 || epoch <= 1000000000LL) return;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (epoch <= s_state.rapid_wind_epoch) { xSemaphoreGive(s_mutex); return; }
    s_state.rapid_wind_ms = speed_ms;
    s_state.rapid_wind_dir = dir_deg;
    s_state.rapid_wind_epoch = epoch;
    s_state.rapid_wind_millis = millis();

    if (speed_ms >= s_state.peak_gust_ms) {
        s_state.peak_gust_ms = speed_ms;
        s_state.peak_gust_dir = dir_deg;
        s_state.peak_gust_epoch = epoch;
    }

    s_state.last_packet_millis = millis();
    s_state.udp_connected = true;
    s_packet_received = true;
    xSemaphoreGive(s_mutex);
}

void tempest_update_strike(float dist_km, uint32_t energy, int64_t epoch) {
    (void)energy;
    if (epoch <= 1000000000LL || !isfinite(dist_km) || dist_km < 0) return;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (!s_ledger.strike(epoch, dist_km)) { xSemaphoreGive(s_mutex); return; }
    s_state.lightning_dist_km = dist_km;
    s_state.last_strike_epoch = epoch;
    s_state.strike_alert_active = true;

    // Shift recent strikes list down and insert newest at index 0
    for (int i = 3; i > 0; --i) {
        s_state.recent_strikes[i] = s_state.recent_strikes[i - 1];
    }
    s_state.recent_strikes[0].dist_km = dist_km;
    s_state.recent_strikes[0].epoch = epoch;
    if (s_state.recent_strike_count < 4) s_state.recent_strike_count++;

    s_packet_received = true;
    s_state.last_packet_millis = millis();
    refresh_ledger(epoch);

    xSemaphoreGive(s_mutex);
}

void tempest_clear_strike_alert() {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_state.strike_alert_active = false;
    xSemaphoreGive(s_mutex);
}

void tempest_update_precip_event(int64_t epoch) {
    if (epoch <= 1000000000LL) return;
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (epoch <= s_state.last_rain_epoch) { xSemaphoreGive(s_mutex); return; }
    s_state.is_raining = true;
    s_state.last_rain_epoch = epoch;
    if (s_state.precip_type == 0) s_state.precip_type = 1; // Rain
    s_state.last_packet_millis = millis();
    s_packet_received = true;
    xSemaphoreGive(s_mutex);
}

void tempest_update_rain_totals(float today_mm, float yesterday_mm, int prob_pct, int64_t epoch) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    int day_before = s_ledger.day;
    float today_before = s_ledger.today;
    float yesterday_before = s_ledger.yesterday;
    bool complete_before = s_ledger.complete;
    int64_t now = time(nullptr);
    if (now <= 1000000000LL) now = epoch;
    if (epoch > 1000000000LL && local_day(epoch) == local_day(now)) {
        s_ledger.rollover(local_day(epoch));
        if (isfinite(today_mm) && today_mm >= 0) s_ledger.reconcile(today_mm, epoch);
        if (isfinite(yesterday_mm) && yesterday_mm >= 0) s_ledger.yesterday = yesterday_mm;
    }
    if (prob_pct >= 0) s_state.rain_probability_pct = prob_pct;
    refresh_ledger(now);
    bool same_yesterday = (isnan(s_ledger.yesterday) && isnan(yesterday_before)) || s_ledger.yesterday == yesterday_before;
    bool changed = s_ledger.day != day_before || s_ledger.today != today_before ||
                   !same_yesterday || s_ledger.complete != complete_before;
    xSemaphoreGive(s_mutex);
    if (changed) history_store();
}

void tempest_update_forecast(const char *conditions, const char *icon,
                            float high_c, float low_c, float feels_c) {
    if (!s_mutex || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    {
        strncpy(s_state.conditions_text, conditions ? conditions : "", sizeof(s_state.conditions_text) - 1);
        s_state.conditions_text[sizeof(s_state.conditions_text) - 1] = '\0';
    }
    {
        strncpy(s_state.icon_slug, icon ? icon : "", sizeof(s_state.icon_slug) - 1);
        s_state.icon_slug[sizeof(s_state.icon_slug) - 1] = '\0';
    }
    s_state.temp_high_c = high_c;
    s_state.temp_low_c = low_c;
    s_state.feels_like_c = feels_c;
    s_state.rest_connected = true;
    s_state.has_forecast = true;
    s_state.last_forecast_millis = millis();
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

float rain_to_unit(float rain_mm, UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? (rain_mm * 0.0393701f) : rain_mm;
}

const char* rain_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "in" : "mm";
}

const char* rain_rate_unit_str(UnitSystem u) {
    return (u == UNIT_IMPERIAL) ? "in/hr" : "mm/hr";
}

const char* wind_cardinal(int degrees) {
    static const char *cardinals[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int idx = (int)((degrees + 11.25f) / 22.5f) & 15;
    return cardinals[idx];
}
