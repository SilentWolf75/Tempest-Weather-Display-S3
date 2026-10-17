#pragma once
#include <stdint.h>
#include <math.h>

// Minute buckets avoid a fixed strike-event cap during a busy storm.
// Expiry resolution is one minute; counts come from deduplicated observation reports.
struct WeatherLedger {
    struct Bucket { int64_t minute = -1; int64_t report_epoch = 0; int count = 0; float rain = 0; float closest = -1; } buckets[181];
    int64_t last_obs = 0;
    int64_t last_strike = 0;
    int day = -1;
    float today = 0;
    float yesterday = NAN;
    int strikes_today = 0;
    bool complete = false;

    void rollover(int day_key) {
        if (day < 0) { day = day_key; return; }
        if (day_key <= day) return;
        yesterday = complete && day_key == day + 1 ? today : NAN;
        today = 0;
        strikes_today = 0;
        complete = false;
        day = day_key;
    }
    Bucket &bucket(int64_t epoch) {
        int64_t minute = epoch / 60;
        Bucket &b = buckets[minute % 181];
        if (b.minute != minute) { b = Bucket{}; b.minute = minute; }
        return b;
    }
    bool observation(int64_t epoch, int day_key, float rain, int strikes) {
        if (epoch <= last_obs) return false;
        rollover(day_key);
        last_obs = epoch;
        today += rain;
        strikes_today += strikes;
        Bucket &b = bucket(epoch);
        b.count += strikes;
        b.rain += rain;
        b.report_epoch = epoch;
        return true;
    }
    bool reconcile(float cloud_total, int64_t through_epoch) {
        if (last_obs > through_epoch && last_obs - through_epoch >= 10800) return false;
        float after = 0;
        for (const auto &b : buckets) if (b.report_epoch > through_epoch) after += b.rain;
        today = cloud_total + after;
        complete = true;
        return true;
    }
    bool strike(int64_t epoch, float distance) {
        if (epoch <= last_strike) return false;
        last_strike = epoch;
        Bucket &b = bucket(epoch);
        if (b.closest < 0 || distance < b.closest) b.closest = distance;
        return true;
    }
    int count(int64_t now) const {
        int total = 0;
        for (const auto &b : buckets)
            if (b.minute >= 0 && now / 60 >= b.minute && now / 60 - b.minute < 180) total += b.count;
        return total;
    }
    float closest(int64_t now) const {
        float result = -1;
        for (const auto &b : buckets)
            if (b.minute >= 0 && now / 60 >= b.minute && now / 60 - b.minute < 180 &&
                b.closest >= 0 && (result < 0 || b.closest < result)) result = b.closest;
        return result;
    }
};
