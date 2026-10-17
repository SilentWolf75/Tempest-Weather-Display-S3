#pragma once
#include "Arduino.h"
#include <map>

inline std::map<std::string, std::string> test_preferences;
class Preferences {
public:
    bool begin(const char *, bool) { return true; }
    String getString(const char *key, const char *fallback) {
        auto i = test_preferences.find(key);
        return i == test_preferences.end() ? String(fallback) : String(i->second);
    }
    unsigned getUInt(const char *key, unsigned fallback) {
        auto i = test_preferences.find(key);
        return i == test_preferences.end() ? fallback : (unsigned)std::stoul(i->second);
    }
    uint8_t getUChar(const char *key, uint8_t fallback) { return (uint8_t)getUInt(key, fallback); }
    uint16_t getUShort(const char *key, uint16_t fallback) { return (uint16_t)getUInt(key, fallback); }
    bool getBool(const char *key, bool fallback) { return getUInt(key, fallback) != 0; }
    bool isKey(const char *key) const { return test_preferences.find(key) != test_preferences.end(); }
    bool remove(const char *key) { return test_preferences.erase(key) > 0; }
    size_t putString(const char *key, const char *value) { test_preferences[key] = value; return strlen(value) ? strlen(value) : 1; }
    size_t putString(const char *key, const String &value) { return putString(key, value.c_str()); }
    size_t putUInt(const char *key, unsigned value) { test_preferences[key] = std::to_string(value); return 4; }
    size_t putUChar(const char *key, uint8_t value) { return putUInt(key, value) ? 1 : 0; }
    size_t putUShort(const char *key, uint16_t value) { return putUInt(key, value) ? 2 : 0; }
    size_t putBool(const char *key, bool value) { return putUInt(key, value) ? 1 : 0; }
    size_t putBytes(const char *key, const void *value, size_t len) {
        test_preferences[key].assign((const char *)value, (const char *)value + len);
        return len;
    }
    size_t getBytes(const char *key, void *buf, size_t maxLen) {
        auto i = test_preferences.find(key);
        if (i == test_preferences.end() || i->second.size() > maxLen) return 0;
        memcpy(buf, i->second.data(), i->second.size());
        return i->second.size();
    }
};
