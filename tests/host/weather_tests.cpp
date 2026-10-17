#include "Arduino.h"
#include "Preferences.h"
#include "tempest_state.h"
#include "brightness_mgr.h"
#include "weather_policy.h"
#include "weather_diagnostics.h"
#include "weather_clock.h"
#include "weather_wind.h"
#include "weather_rain.h"
#include "weather_environment.h"
#include "weather_current.h"
#include "weather_forecast.h"
#include "weather_reset.h"
#include "weather_ledger.h"
#include <cassert>
#include <cmath>
#include <iostream>

uint32_t test_millis = 10000;
time_t test_epoch = 1791072000; // 2026-10-04 00:00 UTC
static int brightness = -1;
namespace display { void setBrightness(uint8_t b) { brightness = b; } }

static TempestState snapshot() { TempestState s; tempest_get_state(&s); return s; }
static void advance(unsigned seconds) { test_epoch += seconds; test_millis += seconds * 1000; tempest_state_tick(); }
static void obs(float rain = 0, int strikes = 0, bool udp = true, time_t epoch = 0) {
    tempest_update_obs(22, 60, 1000, 3, 5, 1, 180, 2, 200, rain, 0, -1, strikes, 2.5,
                       epoch ? epoch : test_epoch, udp);
}

int main() {
    TempestState forecast{};
    auto outlook = weather_forecast(forecast, 0);
    assert(!outlook.fresh && !outlook.received && std::isnan(outlook.high_c));
    forecast.has_forecast = true;
    forecast.last_forecast_millis = 0;
    strcpy(forecast.conditions_text, "Clear");
    forecast.temp_high_c = 0;
    forecast.temp_low_c = -10;
    forecast.feels_like_c = NAN;
    outlook = weather_forecast(forecast, 1199999);
    assert(outlook.fresh && outlook.high_c == 0 && outlook.low_c == -10 && std::isnan(outlook.feels_c));
    assert(strcmp(outlook.conditions, "Clear") == 0);
    outlook = weather_forecast(forecast, 1200000);
    assert(!outlook.fresh && std::isnan(outlook.high_c) && strcmp(outlook.conditions, "Forecast stale") == 0);
    forecast.last_forecast_millis = UINT32_MAX - 4999;
    forecast.conditions_text[0] = '\0';
    outlook = weather_forecast(forecast, 1000);
    assert(outlook.fresh && outlook.age_s == 6 && strcmp(outlook.conditions, "Conditions unavailable") == 0);
    TempestState conditions{};
    auto current = weather_current(conditions, 10000, test_epoch);
    assert(!current.fresh && std::isnan(current.temperature_c) && std::isnan(current.humidity_pct));
    conditions.has_observation = conditions.observation_from_udp = true;
    conditions.last_obs_millis = 10000;
    conditions.last_obs_epoch = test_epoch;
    conditions.air_temp_c = -5;
    conditions.humidity_pct = 100;
    conditions.dew_point_c = -5;
    conditions.pressure_mb = 1000;
    current = weather_current(conditions, 10000, test_epoch);
    assert(current.fresh && current.temperature_c == -5 && current.dew_point_c == -5 && current.humidity_pct == 100);
    current = weather_current(conditions, 100000, test_epoch + 90);
    assert(!current.fresh && std::isnan(current.temperature_c) && std::isnan(current.pressure_mb));
    conditions.observation_from_udp = false;
    assert(weather_current(conditions, 100000, test_epoch + 90).fresh);
    assert(!weather_current(conditions, 1210000, test_epoch + 1200).fresh);
    assert(!weather_current(conditions, 10000, test_epoch + 1200).fresh); // newly received old cloud reading
    conditions.air_temp_c = 0;
    conditions.humidity_pct = 0;
    conditions.dew_point_c = 0; // reject a stale or fabricated derived dew point at zero humidity
    current = weather_current(conditions, 10000, test_epoch);
    assert(current.temperature_c == 0 && current.humidity_pct == 0 && std::isnan(current.dew_point_c));
    conditions.last_obs_millis = UINT32_MAX - 4999;
    conditions.humidity_pct = 101;
    conditions.pressure_mb = -1;
    current = weather_current(conditions, 1000, 0);
    assert(current.fresh && current.age_s == 6 && std::isnan(current.humidity_pct) && std::isnan(current.pressure_mb));
    TempestState environment{};
    auto surroundings = weather_environment(environment, 10000, test_epoch);
    assert(std::isnan(surroundings.uv) && std::isnan(surroundings.battery));
    environment.has_observation = environment.observation_from_udp = environment.has_battery = true;
    environment.last_obs_millis = environment.battery_millis = 10000;
    environment.last_obs_epoch = environment.battery_epoch = test_epoch;
    environment.uv_index = environment.solar_wm2 = 0;
    environment.battery_v = 2.5;
    surroundings = weather_environment(environment, 10000, test_epoch);
    assert(surroundings.uv == 0 && surroundings.solar == 0 && surroundings.battery == 2.5);
    surroundings = weather_environment(environment, 100000, test_epoch + 90);
    assert(std::isnan(surroundings.uv) && std::isnan(surroundings.battery));
    environment.observation_from_udp = false;
    environment.last_obs_millis = 100000;
    environment.last_obs_epoch = test_epoch + 90;
    surroundings = weather_environment(environment, 100000, test_epoch + 90);
    assert(surroundings.uv == 0 && std::isnan(surroundings.battery) && surroundings.battery_age_s == 90);
    assert(std::isnan(weather_environment(environment, 1300000, test_epoch + 1290).solar));
    environment.last_obs_epoch = environment.battery_epoch = test_epoch - 2000;
    assert(std::isnan(weather_environment(environment, 100000, test_epoch).uv));
    environment.last_obs_millis = environment.battery_millis = UINT32_MAX - 4999;
    environment.observation_from_udp = true;
    surroundings = weather_environment(environment, 1000, 0);
    assert(surroundings.battery == 2.5 && surroundings.battery_age_s == 6);
    environment.uv_index = -1;
    environment.solar_wm2 = NAN;
    environment.battery_v = 0;
    surroundings = weather_environment(environment, 1000, 0);
    assert(std::isnan(surroundings.uv) && std::isnan(surroundings.solar) && std::isnan(surroundings.battery));
    assert(strcmp(weather_reset_label(ESP_RST_POWERON), "Power on") == 0);
    assert(strcmp(weather_reset_label(ESP_RST_SW), "Software reset") == 0);
    assert(strcmp(weather_reset_label(ESP_RST_PANIC), "Crash / panic") == 0);
    assert(strcmp(weather_reset_label(ESP_RST_TASK_WDT), "Task watchdog") == 0);
    assert(strcmp(weather_reset_label(ESP_RST_BROWNOUT), "Brownout") == 0);
    assert(strcmp(weather_reset_label(ESP_RST_USB), "USB reset") == 0);
    assert(strcmp(weather_reset_label(static_cast<esp_reset_reason_t>(999)), "Unknown") == 0);
    uint32_t event_age;
    assert(!weather_event_age(0, test_epoch, &event_age));
    assert(!weather_event_age(test_epoch, 0, &event_age));
    assert(!weather_event_age(test_epoch + 1, test_epoch, &event_age));
    assert(weather_event_age(test_epoch, test_epoch, &event_age) && event_age == 0);
    assert(weather_event_age(test_epoch - 172800, test_epoch, &event_age) && event_age == 172800);
    assert(weather_event_age(1, INT64_MAX, &event_age) && event_age == UINT32_MAX);
    char last_strike[32];
    weather_format_last_strike(last_strike, sizeof(last_strike), 0, test_epoch);
    assert(strcmp(last_strike, "Last: Not received") == 0);
    weather_format_last_strike(last_strike, sizeof(last_strike), test_epoch, test_epoch);
    assert(strcmp(last_strike, "Last: 0s ago") == 0);
    weather_format_last_strike(last_strike, sizeof(last_strike), test_epoch - 172800, test_epoch);
    assert(strcmp(last_strike, "Last: 2d 0h ago") == 0);
    weather_format_last_strike(last_strike, sizeof(last_strike), test_epoch, 0);
    assert(strcmp(last_strike, "Last: Time unknown") == 0);
    TempestState rainfall{};
    rainfall.rain_rate_mm_hr = NAN;
    rainfall.rain_yesterday_mm = NAN;
    auto rain = weather_rain(rainfall, 10000, test_epoch);
    assert(std::isnan(rain.rate_mm_hr) && std::isnan(rain.today_mm) && std::isnan(rain.yesterday_mm));
    assert(!rain.active && rain.probability_pct == -1);
    rainfall.rain_today_complete = true;
    rainfall.rain_today_mm = 12;
    assert(weather_rain(rainfall, 10000, test_epoch).today_mm == 12); // known cloud total without a rate
    rainfall.has_observation = rainfall.observation_from_udp = true;
    rainfall.last_obs_millis = 10000;
    rainfall.last_obs_epoch = test_epoch;
    rainfall.rain_rate_mm_hr = 6;
    rainfall.rain_yesterday_mm = 2;
    rainfall.has_forecast = true;
    rainfall.last_forecast_millis = 10000;
    rainfall.rain_probability_pct = 0;
    rain = weather_rain(rainfall, 10000, test_epoch);
    assert(rain.active && rain.intensity == RainIntensity::Moderate && rain.probability_pct == 0);
    rain = weather_rain(rainfall, 100000, test_epoch + 90);
    assert(!rain.active && std::isnan(rain.rate_mm_hr) && rain.intensity == RainIntensity::Unknown);
    assert(rain.today_mm == 12 && rain.yesterday_mm == 2 && rain.complete);
    assert(strcmp(rain.source, "Rain data stale") == 0);
    assert(std::isnan(weather_rain(rainfall, 10000, test_epoch + 90).rate_mm_hr)); // newly received old reading
    rainfall.rain_rate_mm_hr = 0;
    rainfall.is_raining = true; // sticky rain event must not imply a current nonzero rate
    rain = weather_rain(rainfall, 10000, test_epoch);
    assert(!rain.active && rain.intensity == RainIntensity::Dry);
    rainfall.precip_type = 2;
    assert(weather_rain(rainfall, 10000, test_epoch).intensity == RainIntensity::Hail);
    rainfall.precip_type = 3;
    assert(weather_rain(rainfall, 10000, test_epoch).intensity == RainIntensity::Mixed);
    rainfall.precip_type = 0;
    rainfall.rain_rate_mm_hr = 0.01f;
    assert(weather_rain(rainfall, 10000, test_epoch).intensity == RainIntensity::Light);
    rainfall.rain_rate_mm_hr = 50;
    assert(weather_rain(rainfall, 10000, test_epoch).intensity == RainIntensity::VeryHeavy);
    rainfall.rain_rate_mm_hr = -1;
    assert(std::isnan(weather_rain(rainfall, 10000, test_epoch).rate_mm_hr));
    rainfall.rain_probability_pct = 101;
    assert(weather_rain(rainfall, 10000, test_epoch).probability_pct == -1);
    rainfall.rain_probability_pct = 80;
    assert(weather_rain(rainfall, 1210000, test_epoch + 1200).probability_pct == -1);
    rainfall.last_obs_millis = UINT32_MAX - 999;
    rainfall.last_obs_epoch = test_epoch - 2;
    rainfall.rain_rate_mm_hr = 1;
    assert(weather_rain(rainfall, 1000, 0).rate_mm_hr == 1); // unsynchronized clock and millis rollover
    rainfall.observation_from_udp = false;
    rainfall.rain_rate_mm_hr = NAN;
    assert(strcmp(weather_rain(rainfall, 1000, 0).source, "Rain rate unavailable") == 0);
    rainfall.rain_today_complete = false;
    assert(!weather_rain(rainfall, 1000, 0).complete);
    TempestState wind_state{};
    wind_state.wind_avg_ms = NAN;
    assert(!weather_wind(wind_state, 10000, test_epoch).received);
    wind_state.has_observation = wind_state.observation_from_udp = true;
    wind_state.last_obs_millis = 10000;
    wind_state.last_obs_epoch = test_epoch;
    wind_state.wind_avg_ms = 3;
    wind_state.wind_dir_deg = 360;
    auto wind = weather_wind(wind_state, 10000, test_epoch);
    assert(wind.speed_ms == 3 && wind.direction_valid && wind.direction_deg == 0);
    assert(strcmp(wind.source, "Sensor average") == 0);
    wind_state.rapid_wind_ms = 5;
    wind_state.rapid_wind_dir = 90;
    wind_state.rapid_wind_epoch = test_epoch;
    wind_state.rapid_wind_millis = 10000;
    wind = weather_wind(wind_state, 17999, test_epoch + 7);
    assert(wind.speed_ms == 5 && wind.direction_deg == 90 && wind.age_s == 7);
    wind = weather_wind(wind_state, 18000, test_epoch + 8);
    assert(wind.speed_ms == 3 && strcmp(wind.source, "Sensor average") == 0);
    wind = weather_wind(wind_state, 100000, test_epoch + 90);
    assert(std::isnan(wind.speed_ms) && !wind.direction_valid && !wind.observation_fresh);
    assert(strcmp(wind.source, "Wind stale") == 0);
    wind_state.rapid_wind_millis = 100000;
    wind_state.rapid_wind_epoch = test_epoch + 90;
    wind = weather_wind(wind_state, 100000, test_epoch + 90);
    assert(wind.speed_ms == 5 && !wind.observation_fresh);
    wind_state.rapid_wind_epoch = test_epoch; // old rapid packet received just now
    assert(std::isnan(weather_wind(wind_state, 100000, test_epoch + 90).speed_ms));
    wind_state.observation_from_udp = false;
    wind = weather_wind(wind_state, 100000, test_epoch + 90);
    assert(wind.speed_ms == 3 && strcmp(wind.source, "Cloud average") == 0);
    assert(std::isnan(weather_wind(wind_state, 1210000, test_epoch + 1200).speed_ms));
    // An old packet downloaded just now must not look fresh.
    assert(std::isnan(weather_wind(wind_state, 10000, test_epoch + 1201).speed_ms));
    wind_state.rapid_wind_ms = NAN;
    assert(weather_wind(wind_state, 10000, test_epoch).speed_ms == 3);
    wind_state.rapid_wind_epoch = 0;
    wind_state.wind_avg_ms = 0;
    assert(!weather_wind(wind_state, 10000, test_epoch).direction_valid);
    wind_state.wind_avg_ms = 0.49f;
    assert(!weather_wind(wind_state, 10000, test_epoch).direction_valid);
    wind_state.wind_avg_ms = 0.5f;
    assert(weather_wind(wind_state, 10000, test_epoch).direction_valid);
    wind_state.wind_dir_deg = -1;
    assert(!weather_wind(wind_state, 10000, test_epoch).direction_valid);
    wind_state.last_obs_millis = UINT32_MAX - 999;
    wind_state.last_obs_epoch = test_epoch - 2;
    assert(weather_wind(wind_state, 1000, 0).observation_fresh);
    char wind_text[16];
    weather_format_wind(wind_text, sizeof(wind_text), NAN, UNIT_METRIC);
    assert(strcmp(wind_text, "--") == 0);
    weather_format_wind(wind_text, sizeof(wind_text), 0, UNIT_METRIC);
    assert(strcmp(wind_text, "0") == 0);
    weather_format_wind(wind_text, sizeof(wind_text), 10, UNIT_IMPERIAL);
    assert(strcmp(wind_text, "22") == 0);
    char clock[24];
    weather_format_clock(clock, sizeof(clock), 0, 5, 9, false);
    assert(strcmp(clock, "12:05 AM") == 0);
    weather_format_clock(clock, sizeof(clock), 12, 0, 0, false);
    assert(strcmp(clock, "12:00 PM") == 0);
    weather_format_clock(clock, sizeof(clock), 13, 5, 9, false, true);
    assert(strcmp(clock, "1:05:09 PM") == 0);
    weather_format_clock(clock, sizeof(clock), 0, 5, 9, true);
    assert(strcmp(clock, "00:05") == 0);
    weather_format_clock(clock, sizeof(clock), 23, 59, 59, true, true);
    assert(strcmp(clock, "23:59:59") == 0);
    unsigned failures = 0;
    const uint32_t retry_delays[] = {30000, 60000, 120000, 240000, 480000, 600000, 600000};
    for (uint32_t expected : retry_delays)
        assert(weather_cloud_retry_delay(CloudResult::TransportError, failures) == expected);
    assert(weather_cloud_retry_delay(CloudResult::Success, failures) == 600000 && failures == 0);
    assert(weather_cloud_retry_delay(CloudResult::AccessDenied, failures) == 600000);
    assert(weather_cloud_retry_delay(CloudResult::RateLimited, failures, 3600000) == 3600000);
    assert(weather_cloud_retry_delay(CloudResult::SettingsChanged, failures) == 0);
    assert(weather_cloud_http_result(-1) == CloudResult::TransportError);
    assert(weather_cloud_http_result(401) == CloudResult::AccessDenied);
    assert(weather_cloud_http_result(404) == CloudResult::StationMissing);
    assert(weather_cloud_http_result(429) == CloudResult::RateLimited);
    assert(weather_cloud_http_result(503) == CloudResult::ServiceError);
    assert(weather_cloud_retry_after("120") == 120000);
    assert(weather_cloud_retry_after("123invalid") == 0);
    assert(weather_cloud_retry_after("999999999999999999") == 604800000);
    assert(weather_cloud_remaining(1000, UINT32_MAX - 999, 30000) == 28000);
    assert(weather_cloud_due(1000, UINT32_MAX - 999, 30000, true, false));
    assert(!weather_cloud_due(1000, UINT32_MAX - 999, 30000, true, true));
    assert(weather_cloud_due(30000, 0, 30000, false, true));
    TempestState ages{};
    ages.last_obs_millis = UINT32_MAX - 999;
    ages.last_obs_epoch = 1791071900;
    assert(weather_observation_age(ages, 1000, 1791072000) == 100);
    assert(weather_observation_age(ages, 1000, 0) == 2); // rollover, unsynchronized clock
    char age[24];
    weather_format_age(age, sizeof(age), false, 0);
    assert(strcmp(age, "Waiting") == 0);
    weather_format_age(age, sizeof(age), true, 0);
    assert(strcmp(age, "0s ago") == 0);
    weather_format_age(age, sizeof(age), true, 3660);
    assert(strcmp(age, "1h 1m ago") == 0);
    tempest_state_init();
    tempest_cloud_attempt();
    assert(snapshot().cloud_busy && snapshot().cloud_attempted);
    tempest_cloud_finish(CloudResult::RateLimited, 429, 600000, true);
    assert(!snapshot().cloud_busy && snapshot().cloud_result == CloudResult::RateLimited);
    assert(snapshot().cloud_http_code == 429 && snapshot().cloud_service_cooldown);
    test_millis = 0;
    tempest_update_forecast("Clear", "clear-day", 24, 12, 22);
    tempest_state_tick();
    assert(snapshot().has_forecast && snapshot().rest_connected);
    test_millis = 1200000;
    tempest_state_tick();
    assert(snapshot().has_forecast && !snapshot().rest_connected); // even a forecast received at millis=0 expires
    tempest_update_forecast(nullptr, nullptr, NAN, NAN, NAN);
    assert(snapshot().conditions_text[0] == '\0' && snapshot().icon_slug[0] == '\0');
    assert(strcmp(weather_forecast(snapshot(), test_millis).conditions, "Conditions unavailable") == 0);
    test_millis = 10000;
    test_preferences["units"] = "1";
    test_preferences["tz_idx"] = "7";
    tempest_state_init();
    settings_init();
    assert(snapshot().units == UNIT_METRIC); // regression: initialization used to erase saved units
    AppSettings settings;
    settings_get(&settings);
    assert(strlen(settings.admin_password) == 16);
    assert(!settings.clock_24h);
    settings.clock_24h = true;
    assert(settings.startup_screen == 0 && settings.auto_scroll_mask == 15 && settings.night_wake_s == 30);
    settings.startup_screen = 3;
    settings.auto_scroll_mask = 5;
    settings.night_wake_s = 60;
    settings.brightness_dim = 71;
    settings_save(&settings);
    AppSettings saved;
    settings_get(&saved);
    assert(saved.brightness_dim == 71 && test_preferences["b_dim"] == "71");
    assert(saved.clock_24h && test_preferences["clock24"] == "1");
    assert(saved.startup_screen == 3 && saved.auto_scroll_mask == 5 && saved.night_wake_s == 60);
    assert(test_preferences["start_pg"] == "3" && test_preferences["scroll_mask"] == "5" && test_preferences["wake_s"] == "60");
    assert(!snapshot().has_observation && std::isnan(snapshot().air_temp_c));
    assert(snapshot().pressure_hist_count == 0 && snapshot().rain_probability_pct == -1);

    obs(1.25, 80);
    assert(snapshot().has_battery && snapshot().battery_v == 2.5 && snapshot().battery_epoch == test_epoch);
    assert(snapshot().pressure_hist_count == 1 && !snapshot().pressure_trend_valid);
    obs(1.25, 80); // duplicate must not add rain or strikes
    assert(snapshot().rain_today_mm == 1.25f && snapshot().lightning_count_today == 80);
    assert(snapshot().lightning_count_3h == 80); // no 64-strike cap
    obs(10, 10, true, test_epoch - 60); // out of order
    assert(snapshot().rain_today_mm == 1.25f);
    tempest_update_obs(NAN, 50, 1000, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, test_epoch + 1);
    assert(snapshot().air_temp_c == 22);

    tempest_update_strike(4, 100, test_epoch);
    advance(1);
    tempest_update_strike(20, 100, test_epoch);
    assert(snapshot().closest_strike_km == 4); // closest is not simply latest
    assert(snapshot().lightning_count_today == 80); // event and observation must not double count
    assert(snapshot().recent_strike_count == 2);
    tempest_update_strike(20, 100, test_epoch);
    assert(snapshot().recent_strike_count == 2);

    advance(90);
    assert(!snapshot().udp_connected);
    assert(strcmp(tempest_feed_label(snapshot()), "Stale weather data") == 0);
    obs(0, 0, false);
    assert(!snapshot().observation_from_udp && !snapshot().udp_connected);
    assert(strcmp(tempest_feed_label(snapshot()), "Cloud weather data") == 0);
    advance(600);
    tempest_update_obs(25, 65, 1005, 4, 6, 0, 90, 2, 100, 0, 0, -1, 0, 0, test_epoch, false);
    assert(snapshot().air_temp_c == 25); // cloud-only mode refreshes every poll
    assert(snapshot().battery_epoch < snapshot().last_obs_epoch);
    assert(std::isnan(weather_environment(snapshot(), test_millis, test_epoch).battery));
    advance(1);
    obs();
    assert(snapshot().observation_from_udp);
    advance(1);
    tempest_update_obs(99, 65, 1005, 4, 6, 0, 90, 2, 100, 0, 0, -1, 0, 0, test_epoch, false);
    assert(snapshot().air_temp_c == 22); // live sensor wins over cloud
    tempest_update_rapid_wind(7, 270, test_epoch);
    uint32_t rapid = snapshot().rapid_wind_millis;
    advance(9);
    obs(); // another packet does not refresh rapid wind
    assert(!weather_recent(true, test_millis, rapid, 8000));

    for (int i = 0; i < 6; ++i) { advance(1800); obs(); }
    assert(snapshot().pressure_hist_count == 7 && snapshot().pressure_trend_valid);
    for (int i = 1; i < 7; ++i)
        assert(snapshot().pressure_hist_epoch[i] > snapshot().pressure_hist_epoch[i - 1]);
    assert(snapshot().lightning_count_3h == 0 && snapshot().closest_strike_km < 0);
    assert(snapshot().recent_strike_count == 0);
    tempest_update_rain_totals(4, 2, 50, test_epoch);
    assert(snapshot().rain_today_complete && snapshot().rain_today_mm == 4);
    struct tm ti;
    localtime_r(&test_epoch, &ti);
    unsigned to_midnight = 86400 - (ti.tm_hour * 3600 + ti.tm_min * 60 + ti.tm_sec);
    advance(to_midnight);
    assert(snapshot().rain_today_mm == 0 && snapshot().rain_yesterday_mm == 4);
    assert(snapshot().lightning_count_today == 0);
    WeatherLedger ledger;
    ledger.observation(120, 10, 1, 1);
    ledger.observation(180, 10, 2, 2);
    assert(ledger.reconcile(10, 120) && ledger.today == 12);
    assert(ledger.reconcile(12, 180) && ledger.today == 12);
    ledger.rollover(12);
    assert(std::isnan(ledger.yesterday)); // a skipped day is not yesterday

    assert(weather_recent(true, 20, UINT32_MAX - 10, 100));
    assert(!weather_recent(false, 20, 20, 100));
    assert(weather_night_hour(23, 22, 7) && weather_night_hour(6, 22, 7));
    assert(!weather_night_hour(7, 22, 7) && !weather_night_hour(12, 22, 7));
    assert(!weather_night_hour(12, 12, 12));
    assert(weather_next_screen(0, 5) == 2 && weather_next_screen(2, 5) == 0);
    assert(weather_next_screen(1, 1) == 0 && weather_next_screen(0, 1) == 0);
    assert(weather_next_screen(2, 0) == 2 && weather_next_screen(4, 15) == 4);
    assert(weather_next_screen(3, 15) == 0);
    settings.night_mode_enabled = false;
    settings.brightness_day = 210;
    settings.brightness_dim = 35;
    settings.dim_timeout_s = 10;
    brightness_tick(settings, 11000);
    assert(brightness == 35);
    brightness_preview(9);
    brightness_tick(settings, 11000);
    assert(brightness == 9);
    test_millis += 5000;
    brightness_tick(settings, 11000);
    assert(brightness == 35); // preview expires on device even if browser disappears
    settings.night_mode_enabled = true;
    settings.brightness_night = 8;
    settings.night_start_hour = 22;
    settings.night_end_hour = 7;
    test_epoch = 1791072000 + 23 * 3600;
    brightness_tick(settings, 11000);
    assert(brightness == 8); // changes while already idle
    test_epoch += 8 * 3600;
    brightness_tick(settings, 11000);
    assert(brightness == 35);
    settings.brightness_night = 0;
    settings.brightness_dim = 71;
    settings.night_wake_s = 30;
    test_epoch = 1791072000 + 23 * 3600;
    brightness_tick(settings, 90000);
    assert(brightness == 0); // no wake just because the device is powered
    brightness_tick(settings, 0); // touch resets LVGL inactivity
    assert(brightness == 71);
    brightness_tick(settings, 29999);
    assert(brightness == 71);
    brightness_tick(settings, 30000);
    assert(brightness == 0);
    settings.night_wake_s = 0;
    brightness_tick(settings, 0);
    assert(brightness == 0); // explicit disable keeps night brightness
    settings.night_wake_s = 30;
    settings.brightness_dim = 0;
    brightness_tick(settings, 1);
    assert(brightness == BRIGHTNESS_DIM); // zero idle setting must still allow touch wake
    int saved_count = snapshot().pressure_hist_count;
    assert(saved_count == 7 && snapshot().rain_yesterday_mm == 4.f && snapshot().rain_today_mm == 0.f);
    tempest_state_init();
    assert(snapshot().pressure_hist_count == saved_count);
    assert(snapshot().rain_yesterday_mm == 4.f && snapshot().rain_today_mm == 0.f);
    tempest_update_obs(21, 40, 1000, 3, NAN, NAN, -1, NAN, NAN, 0, 0, -1, 0, NAN, test_epoch + 5, false);
    assert(snapshot().air_temp_c == 21.f);
    assert(std::isnan(snapshot().wind_gust_ms) && std::isnan(snapshot().wind_lull_ms));
    assert(snapshot().wind_dir_deg == -1);
    assert(!weather_wind(snapshot(), test_millis, test_epoch).direction_valid);
    tempest_update_obs(30, 40, 1000, 3, NAN, 1, 10, 0, 0, 0, 0, -1, 0, 2.5, test_epoch + 6, true);
    assert(snapshot().air_temp_c == 21.f); // a sensor observation still requires gust, lull, and direction
    std::cout << "Weather state, persistence, freshness, rollover, screen cycle, night touch wake, and brightness tests passed\n";
}
