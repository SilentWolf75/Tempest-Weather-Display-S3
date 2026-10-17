#pragma once
#include <stdint.h>

enum class CloudResult : uint8_t {
    Never, Success, TransportError, AccessDenied, StationMissing, RateLimited,
    ServiceError, InvalidResponse, MemoryError, SettingsChanged
};

inline const char *weather_cloud_result_label(CloudResult result) {
    switch (result) {
        case CloudResult::Success: return "Updated successfully";
        case CloudResult::TransportError: return "Network or secure connection failed";
        case CloudResult::AccessDenied: return "Check station access and API token";
        case CloudResult::StationMissing: return "Station not found";
        case CloudResult::RateLimited: return "Service requested a slower retry";
        case CloudResult::ServiceError: return "Weather service request failed";
        case CloudResult::InvalidResponse: return "Incomplete or invalid weather response";
        case CloudResult::MemoryError: return "Not enough memory for weather response";
        case CloudResult::SettingsChanged: return "Settings changed; old response discarded";
        default: return "No request yet";
    }
}

inline CloudResult weather_cloud_http_result(int code) {
    if (code <= 0) return CloudResult::TransportError;
    if (code == 401 || code == 403) return CloudResult::AccessDenied;
    if (code == 404) return CloudResult::StationMissing;
    if (code == 429) return CloudResult::RateLimited;
    return CloudResult::ServiceError;
}

inline uint32_t weather_cloud_retry_delay(CloudResult result, unsigned &failures, uint32_t retry_after_ms = 0, uint32_t regular_ms = 600000) {
    uint32_t delay;
    if (result == CloudResult::SettingsChanged) { failures = 0; return 0; }
    if (result == CloudResult::Success || result == CloudResult::AccessDenied ||
        result == CloudResult::StationMissing || result == CloudResult::RateLimited) {
        failures = 0;
        delay = regular_ms;
    } else {
        if (failures < 6) ++failures;
        delay = 30000UL << (failures - 1);
        if (delay > regular_ms) delay = regular_ms;
    }
    return retry_after_ms > delay ? retry_after_ms : delay;
}

// Numeric Retry-After headers are bounded to seven days to keep millisecond
// rollover arithmetic unambiguous. HTTP-date headers use the normal retry policy.
inline uint32_t weather_cloud_retry_after(const char *text) {
    if (!text || !*text) return 0;
    uint32_t seconds = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
        if (seconds < 604800) {
            uint32_t next = seconds * 10 + uint32_t(*p - '0');
            seconds = next > 604800 ? 604800 : next;
        }
    }
    return seconds * 1000UL;
}

inline uint32_t weather_cloud_remaining(uint32_t now, uint32_t started, uint32_t interval) {
    uint32_t elapsed = uint32_t(now - started);
    return elapsed >= interval ? 0 : interval - elapsed;
}

inline bool weather_cloud_due(uint32_t now, uint32_t started, uint32_t interval, bool pending, bool cooldown) {
    return weather_cloud_remaining(now, started, interval) == 0 || (pending && !cooldown);
}
