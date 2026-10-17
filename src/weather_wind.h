#pragma once
#include "weather_diagnostics.h"
#include "weather_policy.h"
#include <math.h>

struct WeatherWind {
    float speed_ms = NAN;
    int direction_deg = 0;
    bool direction_valid = false;
    bool observation_fresh = false;
    bool received = false;
    uint32_t age_s = 0;
    const char *source = "Waiting for wind";
};

inline WeatherWind weather_wind(const TempestState &s, uint32_t tick, int64_t epoch) {
    WeatherWind wind;
    uint32_t obs_age = weather_observation_age(s, tick, epoch);
    uint32_t rapid_age = uint32_t(tick - s.rapid_wind_millis) / 1000;
    if (epoch > 1000000000LL && s.rapid_wind_epoch > 0 && epoch > s.rapid_wind_epoch) {
        uint64_t age = uint64_t(epoch - s.rapid_wind_epoch);
        if (age > rapid_age) rapid_age = age > UINT32_MAX ? UINT32_MAX : uint32_t(age);
    }
    bool rapid_received = s.rapid_wind_epoch > 0 && isfinite(s.rapid_wind_ms) && s.rapid_wind_ms >= 0;
    bool obs_received = s.has_observation && isfinite(s.wind_avg_ms) && s.wind_avg_ms >= 0;
    wind.observation_fresh = weather_observation_fresh(s, tick, epoch);
    wind.received = rapid_received || obs_received;
    wind.age_s = rapid_received && (!obs_received || rapid_age < obs_age) ? rapid_age : obs_age;
    if (weather_recent(rapid_received, tick, s.rapid_wind_millis, 8000) && rapid_age < 8) {
        wind.speed_ms = s.rapid_wind_ms;
        wind.direction_deg = s.rapid_wind_dir;
        wind.age_s = rapid_age;
        wind.source = "Rapid wind";
    } else if (obs_received && wind.observation_fresh) {
        wind.speed_ms = s.wind_avg_ms;
        wind.direction_deg = s.wind_dir_deg;
        wind.age_s = obs_age;
        wind.source = s.observation_from_udp ? "Sensor average" : "Cloud average";
    } else if (wind.received) wind.source = "Wind stale";
    // Match the screen's existing Calm band; no directional claim below it.
    wind.direction_valid = isfinite(wind.speed_ms) && wind.speed_ms >= 0.5f && wind.direction_deg >= 0 && wind.direction_deg <= 360;
    if (wind.direction_deg == 360) wind.direction_deg = 0;
    return wind;
}

inline void weather_format_wind(char *out, size_t size, float speed_ms, UnitSystem units) {
    if (isfinite(speed_ms) && speed_ms >= 0) snprintf(out, size, "%.0f", roundf(wind_to_unit(speed_ms, units)));
    else snprintf(out, size, "--");
}
