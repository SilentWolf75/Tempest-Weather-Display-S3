#pragma once
#include "tempest_state.h"
#include "weather_policy.h"
#include <math.h>

struct WeatherForecast {
    float high_c = NAN;
    float low_c = NAN;
    float feels_c = NAN;
    bool received = false;
    bool fresh = false;
    uint32_t age_s = 0;
    const char *conditions = "Forecast unavailable";
    const char *source = "Waiting for forecast";
};

inline WeatherForecast weather_forecast(const TempestState &s, uint32_t tick) {
    WeatherForecast result;
    result.received = s.has_forecast;
    result.age_s = uint32_t(tick - s.last_forecast_millis) / 1000;
    result.fresh = weather_recent(s.has_forecast, tick, s.last_forecast_millis, 1200000);
    if (!result.received) return result;
    if (!result.fresh) {
        result.source = result.conditions = "Forecast stale";
        return result;
    }
    result.source = "Cloud outlook";
    result.conditions = s.conditions_text[0] ? s.conditions_text : "Conditions unavailable";
    if (isfinite(s.temp_high_c)) result.high_c = s.temp_high_c;
    if (isfinite(s.temp_low_c)) result.low_c = s.temp_low_c;
    if (isfinite(s.feels_like_c)) result.feels_c = s.feels_like_c;
    return result;
}
