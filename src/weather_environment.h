#pragma once
#include "weather_diagnostics.h"
#include <math.h>

struct WeatherEnvironment {
    float uv = NAN;
    float solar = NAN;
    float battery = NAN;
    bool received = false;
    bool battery_received = false;
    uint32_t age_s = 0;
    uint32_t battery_age_s = 0;
    const char *source = "Waiting for environment data";
    const char *battery_source = "Waiting for sensor battery";
};

inline WeatherEnvironment weather_environment(const TempestState &s, uint32_t tick, int64_t epoch) {
    WeatherEnvironment result;
    result.received = s.has_observation;
    result.age_s = weather_observation_age(s, tick, epoch);
    if (s.has_observation) {
        if (weather_observation_fresh(s, tick, epoch)) {
            result.source = s.observation_from_udp ? "Live sensor data" : "Cloud weather data";
            if (isfinite(s.uv_index) && s.uv_index >= 0) result.uv = s.uv_index;
            if (isfinite(s.solar_wm2) && s.solar_wm2 >= 0) result.solar = s.solar_wm2;
        } else result.source = "Environment data stale";
    }
    result.battery_received = s.has_battery;
    // Cloud observations do not contain battery voltage. Use its own sensor timestamp.
    TempestState battery_clock{};
    battery_clock.last_obs_millis = s.battery_millis;
    battery_clock.last_obs_epoch = s.battery_epoch;
    result.battery_age_s = weather_observation_age(battery_clock, tick, epoch);
    if (s.has_battery) {
        if (!weather_recent(true, tick, s.battery_millis, 90000) || result.battery_age_s >= 90)
            result.battery_source = "Sensor battery stale";
        else if (!isfinite(s.battery_v) || s.battery_v <= 0)
            result.battery_source = "Sensor battery unavailable";
        else {
            result.battery = s.battery_v;
            result.battery_source = "Live sensor battery";
        }
    }
    return result;
}
