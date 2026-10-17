#pragma once
#include "tempest_state.h"
#include "weather_policy.h"
#include <stdio.h>
#include <stddef.h>

// Include an old cloud observation's age, rather than just time since download.
inline uint32_t weather_observation_age(const TempestState &s, uint32_t now, int64_t epoch) {
    uint32_t elapsed = uint32_t(now - s.last_obs_millis) / 1000;
    if (epoch > 1000000000LL && s.last_obs_epoch > 0 && epoch > s.last_obs_epoch) {
        uint64_t age = uint64_t(epoch - s.last_obs_epoch);
        if (age > elapsed) return age > UINT32_MAX ? UINT32_MAX : uint32_t(age);
    }
    return elapsed;
}

inline bool weather_observation_fresh(const TempestState &s, uint32_t tick, int64_t epoch) {
    const uint32_t limit = s.observation_from_udp ? 90000 : 1200000;
    return weather_recent(s.has_observation, tick, s.last_obs_millis, limit) &&
        weather_observation_age(s, tick, epoch) < limit / 1000;
}

inline void weather_format_age(char *out, size_t size, bool received, uint32_t seconds) {
    if (!received) snprintf(out, size, "Waiting");
    else if (seconds < 60) snprintf(out, size, "%us ago", unsigned(seconds));
    else if (seconds < 3600) snprintf(out, size, "%um ago", unsigned(seconds / 60));
    else if (seconds < 86400) snprintf(out, size, "%uh %um ago", unsigned(seconds / 3600), unsigned(seconds % 3600 / 60));
    else snprintf(out, size, "%ud %uh ago", unsigned(seconds / 86400), unsigned(seconds % 86400 / 3600));
}

inline bool weather_event_age(int64_t event_epoch, int64_t now, uint32_t *seconds) {
    if (!seconds || event_epoch <= 0 || now < 1000000000LL || now < event_epoch) return false;
    uint64_t age = uint64_t(now - event_epoch);
    *seconds = age > UINT32_MAX ? UINT32_MAX : uint32_t(age);
    return true;
}

inline void weather_format_last_strike(char *out, size_t size, int64_t strike, int64_t now) {
    if (strike <= 0) { snprintf(out, size, "Last: Not received"); return; }
    uint32_t seconds;
    if (!weather_event_age(strike, now, &seconds)) { snprintf(out, size, "Last: Time unknown"); return; }
    char age[24];
    weather_format_age(age, sizeof(age), true, seconds);
    snprintf(out, size, "Last: %s", age);
}
