#pragma once
#include "weather_diagnostics.h"
#include <math.h>

enum class RainIntensity { Unknown, Dry, Light, Moderate, Heavy, VeryHeavy, Hail, Mixed };
struct WeatherRain {
    float rate_mm_hr = NAN;
    float today_mm = NAN;
    float yesterday_mm = NAN;
    int probability_pct = -1;
    bool complete = false;
    bool active = false;
    bool received = false;
    uint32_t age_s = 0;
    RainIntensity intensity = RainIntensity::Unknown;
    const char *source = "Waiting for rain data";
};

inline WeatherRain weather_rain(const TempestState &s, uint32_t tick, int64_t epoch) {
    WeatherRain rain;
    rain.received = s.has_observation;
    rain.age_s = weather_observation_age(s, tick, epoch);
    rain.complete = s.rain_today_complete;
    if ((s.has_observation || rain.complete) && isfinite(s.rain_today_mm) && s.rain_today_mm >= 0)
        rain.today_mm = s.rain_today_mm;
    if (isfinite(s.rain_yesterday_mm) && s.rain_yesterday_mm >= 0) rain.yesterday_mm = s.rain_yesterday_mm;
    if (weather_recent(s.has_forecast, tick, s.last_forecast_millis, 1200000) &&
        s.rain_probability_pct >= 0 && s.rain_probability_pct <= 100) rain.probability_pct = s.rain_probability_pct;
    if (!s.has_observation) return rain;
    if (!weather_observation_fresh(s, tick, epoch)) {
        rain.source = "Rain data stale";
        return rain;
    }
    if (!isfinite(s.rain_rate_mm_hr) || s.rain_rate_mm_hr < 0) {
        rain.source = "Rain rate unavailable";
        return rain;
    }
    rain.rate_mm_hr = s.rain_rate_mm_hr;
    rain.source = s.observation_from_udp ? "Live sensor data" : "Cloud weather data";
    rain.active = rain.rate_mm_hr > 0 || s.precip_type == 2 || s.precip_type == 3;
    rain.intensity = s.precip_type == 2 ? RainIntensity::Hail : s.precip_type == 3 ? RainIntensity::Mixed :
        rain.rate_mm_hr == 0 ? RainIntensity::Dry : rain.rate_mm_hr < 2.5f ? RainIntensity::Light :
        rain.rate_mm_hr < 7.6f ? RainIntensity::Moderate : rain.rate_mm_hr < 50 ? RainIntensity::Heavy : RainIntensity::VeryHeavy;
    return rain;
}

inline const char *weather_rain_intensity(RainIntensity intensity) {
    switch (intensity) {
        case RainIntensity::Dry: return "Dry";
        case RainIntensity::Light: return "Light Rain";
        case RainIntensity::Moderate: return "Moderate Rain";
        case RainIntensity::Heavy: return "Heavy Rain";
        case RainIntensity::VeryHeavy: return "Very Heavy Rain";
        case RainIntensity::Hail: return "Hail";
        case RainIntensity::Mixed: return "Rain + Hail";
        default: return "Rate unavailable";
    }
}
