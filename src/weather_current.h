#pragma once
#include "weather_diagnostics.h"
#include <math.h>

struct WeatherCurrent {
    float temperature_c = NAN;
    float humidity_pct = NAN;
    float dew_point_c = NAN;
    float pressure_mb = NAN;
    bool received = false;
    bool fresh = false;
    uint32_t age_s = 0;
    const char *source = "Waiting for weather";
};

inline WeatherCurrent weather_current(const TempestState &s, uint32_t tick, int64_t epoch) {
    WeatherCurrent current;
    current.received = s.has_observation;
    current.age_s = weather_observation_age(s, tick, epoch);
    current.fresh = weather_observation_fresh(s, tick, epoch);
    if (!current.received) return current;
    if (!current.fresh) { current.source = "Stale weather data"; return current; }
    current.source = s.observation_from_udp ? "Live sensor data" : "Cloud weather data";
    if (isfinite(s.air_temp_c)) current.temperature_c = s.air_temp_c;
    if (isfinite(s.humidity_pct) && s.humidity_pct >= 0 && s.humidity_pct <= 100)
        current.humidity_pct = s.humidity_pct;
    if (isfinite(current.temperature_c) && current.humidity_pct > 0 && isfinite(s.dew_point_c))
        current.dew_point_c = s.dew_point_c;
    if (isfinite(s.pressure_mb) && s.pressure_mb > 0) current.pressure_mb = s.pressure_mb;
    return current;
}
