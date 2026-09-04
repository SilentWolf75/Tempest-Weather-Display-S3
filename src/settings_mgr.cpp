#include "settings_mgr.h"
#include "config.h"
#include "tempest_state.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

static Preferences s_prefs;
static AppSettings s_settings;
static bool s_initialized = false;

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
    if (s_settings.tz_preset < 8) {
        return TZ_POSIX_TABLE[s_settings.tz_preset];
    }
    if (strlen(s_settings.tz_custom) > 0) {
        return s_settings.tz_custom;
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

    String zip = s_prefs.getString("zip", "");
    strncpy(s_settings.zipcode, zip.c_str(), sizeof(s_settings.zipcode) - 1);
    s_settings.zipcode[sizeof(s_settings.zipcode) - 1] = '\0';

    s_settings.station_id = s_prefs.getUInt("st_id", DEFAULT_TEMPEST_STATION_ID);

    String tok = s_prefs.getString("token", DEFAULT_TEMPEST_API_TOKEN);
    strncpy(s_settings.api_token, tok.c_str(), sizeof(s_settings.api_token) - 1);
    s_settings.api_token[sizeof(s_settings.api_token) - 1] = '\0';

    s_settings.units = s_prefs.getUChar("units", (uint8_t)DEFAULT_UNIT_SYSTEM);
    s_settings.brightness_day = s_prefs.getUChar("b_day", BRIGHTNESS_DEFAULT);
    s_settings.brightness_dim = s_prefs.getUChar("b_dim", BRIGHTNESS_DIM);
    s_settings.dim_timeout_s = s_prefs.getUShort("dim_s", 45); // default 45 seconds
    s_settings.auto_scroll_s = s_prefs.getUShort("scroll_s", 10); // default 10s auto-scroll
    s_settings.screen_rotation = s_prefs.getUShort("rot", DEFAULT_ROTATION);

    s_initialized = true;

    // Apply timezone and unit system to state
    settings_apply_timezone();
    tempest_set_units((UnitSystem)s_settings.units);

    Serial.printf("[settings] Loaded settings: Station %u, Zip %s, TZ %s, Units %s, Rot %u deg\n",
                  s_settings.station_id, s_settings.zipcode,
                  settings_get_tz_name(s_settings.tz_preset),
                  (s_settings.units == 0 ? "Imperial" : "Metric"),
                  s_settings.screen_rotation);
}

void settings_get(AppSettings *out) {
    if (!out) return;
    if (!s_initialized) settings_init();
    *out = s_settings;
}

void settings_save(const AppSettings *in) {
    if (!in) return;
    if (!s_initialized) settings_init();

    bool tz_changed = (in->tz_preset != s_settings.tz_preset ||
                       strncmp(in->tz_custom, s_settings.tz_custom, sizeof(s_settings.tz_custom)) != 0);
    bool units_changed = (in->units != s_settings.units);

    s_settings = *in;

    s_prefs.putString("wifi_ssid", s_settings.wifi_ssid);
    s_prefs.putString("wifi_pass", s_settings.wifi_password);
    s_prefs.putUChar("tz_idx", s_settings.tz_preset);
    s_prefs.putString("tz_str", s_settings.tz_custom);
    s_prefs.putString("zip", s_settings.zipcode);
    s_prefs.putUInt("st_id", s_settings.station_id);
    s_prefs.putString("token", s_settings.api_token);
    s_prefs.putUChar("units", s_settings.units);
    s_prefs.putUChar("b_day", s_settings.brightness_day);
    s_prefs.putUChar("b_dim", s_settings.brightness_dim);
    s_prefs.putUShort("dim_s", s_settings.dim_timeout_s);
    s_prefs.putUShort("scroll_s", s_settings.auto_scroll_s);
    s_prefs.putUShort("rot", s_settings.screen_rotation);

    if (tz_changed) {
        settings_apply_timezone();
    }
    if (units_changed) {
        tempest_set_units((UnitSystem)s_settings.units);
    }

    Serial.println("[settings] Saved new settings to NVS flash successfully.");
}
