#pragma once
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <string>
#include <mutex>

extern uint32_t test_millis;
extern time_t test_epoch;
inline uint32_t millis() { return test_millis; }
inline time_t host_time(time_t *out) { if (out) *out = test_epoch; return test_epoch; }
#define time(...) host_time(__VA_ARGS__)
#ifdef _WIN32
inline tm *localtime_r(const time_t *t, tm *out) { return localtime_s(out, t) == 0 ? out : nullptr; }
inline int setenv(const char *key, const char *value, int) { return _putenv_s(key, value); }
#define tzset _tzset
#endif

using SemaphoreHandle_t = std::mutex *;
constexpr int pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t m, uint32_t) { m->lock(); return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t m) { m->unlock(); }
#define pdMS_TO_TICKS(x) (x)

struct HostSerial {
    template<typename... Args> void printf(const char *, Args...) {}
    void println(const char *) {}
};
inline HostSerial Serial;
using String = std::string;
inline void delay(unsigned) {}
inline size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size) { size_t n = len < size - 1 ? len : size - 1; memcpy(dst, src, n); dst[n] = 0; }
    return len;
}
