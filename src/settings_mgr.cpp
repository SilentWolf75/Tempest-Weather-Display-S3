#include "settings_mgr.h"
#include "config.h"
#include "tempest_state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <esp_system.h>

static Preferences s_prefs;
static AppSettings s_settings;
static bool s_initialized = false;
static SemaphoreHandle_t s_settings_mutex = nullptr;

static const char* TZ_POSIX_TABLE[] = {
    "EST5EDT,M3.2.0,M11.1.0",   // 0: Eastern
    "CST6CDT,M3.2.0,M11.1.0",   // 1: Central
    "MST7MDT,M3.2.0,M11.1.0",   // 2: Mountain
    "MST7",                     // 3: Arizona (no DST)
    "PST8PDT,M3.2.0,M11.1.0",   // 4: Pacific
    "AKST9AKDT,M3.2.0,M11.1.0", // 5: Alaska
    "HST10",                    // 6: Hawaii
    "UTC0"                      // 7: UTC
};

static const char* TZ_NAME_TABLE[] = {
    "US Eastern (ET)",
    "US Central (CT)",
    "US Mountain (MT)",
    "US Arizona (MST)",
    "US Pacific (PT)",
    "Alaska (AKT)",
    "Hawaii (HST)",
    "Coordinated Universal Time (UTC)",
    "Custom POSIX"
};

const char* settings_get_tz_name(uint8_t preset) {
    if (preset <= 8) return TZ_NAME_TABLE[preset];
    return "Unknown";
}

const char* settings_get_tz_posix() {
    AppSettings s;
    settings_get(&s);
    if (s.tz_preset < 8) {
        return TZ_POSIX_TABLE[s.tz_preset];
    }
    static thread_local char custom[64];
    if (strlen(s.tz_custom) > 0) {
        memcpy(custom, s.tz_custom, sizeof(custom));
        return custom;
    }
    return TZ_POSIX_TABLE[1]; // default to Central
}

void settings_apply_timezone() {
    const char *tz = settings_get_tz_posix();
    setenv("TZ", tz, 1);
    tzset();
    Serial.printf("[settings] Timezone applied: %s (%s)\n", tz, settings_get_tz_name(s_settings.tz_preset));
}

void settings_init() {
    if (s_initialized) return;

    s_prefs.begin("wxcfg", false);
    s_settings_mutex = xSemaphoreCreateMutex();
    if (!s_settings_mutex) abort();
    String admin = s_prefs.getString("admin", "");
    if (admin.length() != 16) {
        char password[17];
        snprintf(password, sizeof(password), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
        admin = password;
        s_prefs.putString("admin", admin);
    }
    strlcpy(s_settings.admin_password, admin.c_str(), sizeof(s_settings.admin_password));

    String ssid = s_prefs.getString("wifi_ssid", DEFAULT_WIFI_SSID);
    if (ssid.length() == 0) ssid = DEFAULT_WIFI_SSID;
    strncpy(s_settings.wifi_ssid, ssid.c_str(), sizeof(s_settings.wifi_ssid) - 1);
    s_settings.wifi_ssid[sizeof(s_settings.wifi_ssid) - 1] = '\0';

    String pass = s_prefs.getString("wifi_pass", DEFAULT_WIFI_PASS);
    if (pass.length() == 0) pass = DEFAULT_WIFI_PASS;
    strncpy(s_settings.wifi_password, pass.c_str(), sizeof(s_settings.wifi_password) - 1);
    s_settings.wifi_password[sizeof(s_settings.wifi_password) - 1] = '\0';

    s_settings.tz_preset = s_prefs.getUChar("tz_idx", 1); // default Central
    if (s_settings.tz_preset > 8) s_settings.tz_preset = 1;

    String custom_tz = s_prefs.getString("tz_str", "");
    strncpy(s_settings.tz_custom, custom_tz.c_str(), sizeof(s_settings.tz_custom) - 1);
    s_settings.tz_custom[sizeof(s_settings.tz_custom) - 1] = '\0';

    s_settings.station_id = s_prefs.getUInt("st_id", DEFAULT_TEMPEST_STATION_ID);
    strlcpy(s_settings.device_serial, s_prefs.getString("dev_sn", "").c_str(), sizeof(s_settings.device_serial));

    String tok = s_prefs.getString("token", DEFAULT_TEMPEST_API_TOKEN);
    strncpy(s_settings.api_token, tok.c_str(), sizeof(s_settings.api_token) - 1);
    s_settings.api_token[sizeof(s_settings.api_token) - 1] = '\0';

    s_settings.units = s_prefs.getUChar("units", (uint8_t)DEFAULT_UNIT_SYSTEM);
    s_settings.clock_24h = s_prefs.getBool("clock24", false);
    s_settings.brightness_day = s_prefs.getUChar("b_day", BRIGHTNESS_DEFAULT);
    s_settings.brightness_dim = s_prefs.getUChar("b_dim", BRIGHTNESS_DIM);
    s_settings.dim_timeout_s = s_prefs.getUShort("dim_s", 45); // default 45 seconds
    s_settings.auto_scroll_s = s_prefs.getUShort("scroll_s", 10); // default 10s auto-scroll
    s_settings.startup_screen = s_prefs.getUChar("start_pg", 0);
    if (s_settings.startup_screen > 4) s_settings.startup_screen = 0;
    s_settings.auto_scroll_mask = s_prefs.getUChar("scroll_mask", 15) & 15;
    s_settings.screen_rotation = s_prefs.getUShort("rot", DEFAULT_ROTATION);
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    // Earlier firmware stored rotation but always drew this panel at 0°.
    if (!s_prefs.isKey("rot_live")) {
        s_settings.screen_rotation = 0;
        s_prefs.putUShort("rot", 0);
        s_prefs.putBool("rot_live", true);
    }
#endif
    s_settings.night_mode_enabled = s_prefs.getBool("nm_en", true);
    s_settings.night_start_hour = s_prefs.getUChar("nm_start", 22); // 10 PM
    s_settings.night_end_hour = s_prefs.getUChar("nm_end", 7);      // 7 AM
    s_settings.brightness_night = s_prefs.getUChar("b_night", 8);   // Very dim OLED night level
    s_settings.night_wake_s = s_prefs.getUShort("wake_s", 30);
    if (s_settings.night_wake_s > 300) s_settings.night_wake_s = 30;
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    if (s_settings.brightness_dim && s_settings.brightness_dim < BACKLIGHT_MIN_ON) s_settings.brightness_dim = BACKLIGHT_MIN_ON;
    if (s_settings.brightness_night && s_settings.brightness_night < BACKLIGHT_MIN_ON) s_settings.brightness_night = BACKLIGHT_MIN_ON;
#endif

    s_initialized = true;

    // Apply timezone and unit system to state
    settings_apply_timezone();
    tempest_set_units((UnitSystem)s_settings.units);

    Serial.printf("[settings] Loaded settings: Station %u, TZ %s, Units %s, Rot %u deg\n",
                  s_settings.station_id,
                  settings_get_tz_name(s_settings.tz_preset),
                  (s_settings.units == 0 ? "Imperial" : "Metric"),
                  s_settings.screen_rotation);
}

void settings_get(AppSettings *out) {
    if (!out) return;
    if (!s_initialized) settings_init();
    xSemaphoreTake(s_settings_mutex, portMAX_DELAY);
    *out = s_settings;
    xSemaphoreGive(s_settings_mutex);
}

static bool store_text(const char *key, const char *value) {
    if (!value[0]) {
        if (!s_prefs.isKey(key)) return true;
        return s_prefs.remove(key);
    }
    return s_prefs.putString(key, value) > 0;
}

bool settings_save(const AppSettings *in) {
    if (!in) return false;
    if (!s_initialized) settings_init();
    xSemaphoreTake(s_settings_mutex, portMAX_DELAY);

    AppSettings previous = s_settings;
    bool tz_changed = (in->tz_preset != previous.tz_preset ||
                       strncmp(in->tz_custom, previous.tz_custom, sizeof(previous.tz_custom)) != 0);
    bool units_changed = (in->units != previous.units);
    s_settings = *in;
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    if (s_settings.brightness_dim && s_settings.brightness_dim < BACKLIGHT_MIN_ON) s_settings.brightness_dim = BACKLIGHT_MIN_ON;
    if (s_settings.brightness_night && s_settings.brightness_night < BACKLIGHT_MIN_ON) s_settings.brightness_night = BACKLIGHT_MIN_ON;
#endif

    bool ok = true;
    if (strcmp(s_settings.wifi_ssid, previous.wifi_ssid) && !store_text("wifi_ssid", s_settings.wifi_ssid)) ok = false;
    if (strcmp(s_settings.wifi_password, previous.wifi_password) && !store_text("wifi_pass", s_settings.wifi_password)) ok = false;
    if (s_settings.tz_preset != previous.tz_preset && !s_prefs.putUChar("tz_idx", s_settings.tz_preset)) ok = false;
    if (strcmp(s_settings.tz_custom, previous.tz_custom) && !store_text("tz_str", s_settings.tz_custom)) ok = false;
    if (s_settings.station_id != previous.station_id && !s_prefs.putUInt("st_id", s_settings.station_id)) ok = false;
    if (strcmp(s_settings.device_serial, previous.device_serial) && !store_text("dev_sn", s_settings.device_serial)) ok = false;
    if (strcmp(s_settings.api_token, previous.api_token) && !store_text("token", s_settings.api_token)) ok = false;
    if (s_settings.units != previous.units && !s_prefs.putUChar("units", s_settings.units)) ok = false;
    if (s_settings.clock_24h != previous.clock_24h && !s_prefs.putBool("clock24", s_settings.clock_24h)) ok = false;
    if (s_settings.brightness_day != previous.brightness_day && !s_prefs.putUChar("b_day", s_settings.brightness_day)) ok = false;
    if (s_settings.brightness_dim != previous.brightness_dim && !s_prefs.putUChar("b_dim", s_settings.brightness_dim)) ok = false;
    if (s_settings.dim_timeout_s != previous.dim_timeout_s && !s_prefs.putUShort("dim_s", s_settings.dim_timeout_s)) ok = false;
    if (s_settings.auto_scroll_s != previous.auto_scroll_s && !s_prefs.putUShort("scroll_s", s_settings.auto_scroll_s)) ok = false;
    if (s_settings.startup_screen != previous.startup_screen && !s_prefs.putUChar("start_pg", s_settings.startup_screen)) ok = false;
    if (s_settings.auto_scroll_mask != previous.auto_scroll_mask && !s_prefs.putUChar("scroll_mask", s_settings.auto_scroll_mask)) ok = false;
    if (s_settings.screen_rotation != previous.screen_rotation && !s_prefs.putUShort("rot", s_settings.screen_rotation)) ok = false;
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    if (ok && !s_prefs.isKey("rot_live") && !s_prefs.putBool("rot_live", true)) ok = false;
#endif
    if (s_settings.night_mode_enabled != previous.night_mode_enabled && !s_prefs.putBool("nm_en", s_settings.night_mode_enabled)) ok = false;
    if (s_settings.night_start_hour != previous.night_start_hour && !s_prefs.putUChar("nm_start", s_settings.night_start_hour)) ok = false;
    if (s_settings.night_end_hour != previous.night_end_hour && !s_prefs.putUChar("nm_end", s_settings.night_end_hour)) ok = false;
    if (s_settings.brightness_night != previous.brightness_night && !s_prefs.putUChar("b_night", s_settings.brightness_night)) ok = false;
    if (s_settings.night_wake_s != previous.night_wake_s && !s_prefs.putUShort("wake_s", s_settings.night_wake_s)) ok = false;
    UnitSystem units = (UnitSystem)s_settings.units;
    xSemaphoreGive(s_settings_mutex);

    if (tz_changed) settings_apply_timezone();
    if (units_changed) tempest_set_units(units);
    Serial.println(ok ? "[settings] Saved changed settings." : "[settings] NVS write failed.");
    return ok;
}
