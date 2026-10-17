#pragma once
#include <ArduinoJson.h>
#include <math.h>

inline float weather_cloud_number(JsonVariantConst value) {
    if (!value.is<float>()) return NAN;
    float number = value.as<float>();
    return isfinite(number) ? number : NAN;
}

inline bool weather_cloud_valid_current(JsonObjectConst current) {
    float t = weather_cloud_number(current["air_temperature"]);
    float h = weather_cloud_number(current["relative_humidity"]);
    float p = weather_cloud_number(current["station_pressure"]);
    float w = weather_cloud_number(current["wind_avg"]);
    if (!current["wind_gust"].isNull()) {
        float gust = weather_cloud_number(current["wind_gust"]);
        if (!isfinite(gust) || gust < 0) return false;
    }
    if (!current["wind_direction"].isNull() && (!current["wind_direction"].is<int>() ||
        current["wind_direction"].as<int>() < 0 || current["wind_direction"].as<int>() > 360)) return false;
    return !current.isNull() && current["time"].is<int64_t>() && current["time"].as<int64_t>() > 1000000000LL &&
           isfinite(t) && isfinite(h) && h >= 0 && h <= 100 && isfinite(p) && p > 0 && isfinite(w) && w >= 0;
}

inline bool weather_cloud_valid_forecast(JsonObjectConst current, JsonObjectConst forecast) {
    const char *conditions = current["conditions"] | "";
    const char *icon = current["icon"] | "";
    JsonArrayConst daily = forecast["daily"].as<JsonArrayConst>();
    return !forecast.isNull() && daily.size() > 0 && daily[0].is<JsonObjectConst>() && (conditions[0] || icon[0]);
}
