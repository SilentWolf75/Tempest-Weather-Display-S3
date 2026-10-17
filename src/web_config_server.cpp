#include "web_config_server.h"
#include "settings_mgr.h"
#include "tempest_state.h"
#include "tempest_rest.h"
#include "display.h"
#include "config.h"
#include "ui/ui.h"
#include "brightness_mgr.h"
#include "weather_diagnostics.h"
#include "weather_clock.h"
#include "weather_wind.h"
#include "weather_rain.h"
#include "weather_environment.h"
#include "weather_current.h"
#include "weather_forecast.h"
#include "weather_reset.h"
#include "weather_ota.h"
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <time.h>
#include <math.h>

static WebServer s_server(80);
static bool s_server_started = false;
static uint32_t s_wifi_change_ms = 0;
static bool s_wifi_change_pending = false;
static bool s_ota_started = false;
static bool s_ota_complete = false;
static bool s_ota_armed = false;
static bool s_ota_rejected = false;
static bool s_ota_header_ok = false;
static bool s_ota_marked = false;
static bool s_setup_ap = false;
static uint8_t s_ota_prefix[112];
static size_t s_ota_prefix_len = 0;
static uint8_t s_ota_tail[32];
static size_t s_ota_tail_len = 0;

static bool authorized(bool challenge = true) {
    AppSettings s;
    settings_get(&s);
    if (!s_server.authenticate("admin", s.admin_password)) {
        if (challenge) s_server.requestAuthentication(DIGEST_AUTH, "Weather Display", "Use admin and the password shown on the Info screen.");
        return false;
    }
    String origin = s_server.header("Origin");
    if (origin.length() && origin != String("http://") + s_server.hostHeader()) {
        if (challenge) s_server.send(403, "application/json", "{\"status\":\"error\",\"message\":\"Invalid request origin\"}");
        return false;
    }
    s_server.sendHeader("Cache-Control", "no-store");
    return true;
}

static bool valid_settings_payload(JsonDocument &doc) {
    struct Bound { const char *key; int low; int high; };
    const Bound bounds[] = {{"units",0,1}, {"tz_preset",0,8}, {"brightness_day",0,255},
        {"brightness_dim",0,255}, {"brightness_night",0,255}, {"preview_dim",0,255},
        {"night_start_hour",0,23}, {"night_end_hour",0,23}, {"dim_timeout_s",0,3600},
        {"auto_scroll_s",0,3600}, {"screen_rotation",0,270}, {"startup_screen",0,4},
        {"auto_scroll_mask",0,15}, {"night_wake_s",0,300}};
    for (const auto &b : bounds) {
        if (!doc[b.key].isNull() && (!doc[b.key].is<int>() || doc[b.key].as<int>() < b.low || doc[b.key].as<int>() > b.high)) return false;
    }
    if (!doc["screen_rotation"].isNull() && doc["screen_rotation"].as<int>() % 90) return false;
    struct TextBound { const char *key; size_t max; };
    const TextBound texts[] = {{"wifi_ssid",32}, {"wifi_pass",64}, {"tz_custom",63},
                              {"api_token",127}, {"device_serial",23}};
    for (const auto &t : texts) {
        if (!doc[t.key].isNull() && (!doc[t.key].is<const char*>() || strlen(doc[t.key].as<const char*>()) > t.max)) return false;
    }
    if (!doc["station_id"].isNull() && !doc["station_id"].is<uint32_t>()) return false;
    if (!doc["night_mode_enabled"].isNull() && !doc["night_mode_enabled"].is<bool>()) return false;
    if (!doc["clock_24h"].isNull() && !doc["clock_24h"].is<bool>()) return false;
    if (!doc["wifi_open"].isNull() && !doc["wifi_open"].is<bool>()) return false;
    if (!doc["clear_api_token"].isNull() && !doc["clear_api_token"].is<bool>()) return false;
    return doc.is<JsonObject>();
}

extern const char dashboard_html_start[] asm("_binary_src_dashboard_html_start");
extern const char dashboard_html_end[] asm("_binary_src_dashboard_html_end");

static void handle_root() {
    if (!authorized()) return;
    s_server.send_P(200, "text/html", dashboard_html_start, (size_t)(dashboard_html_end - dashboard_html_start));
}

static void handle_api_status() {
    if (!authorized()) return;
    AppSettings s;
    settings_get(&s);

    TempestState wx;
    tempest_get_state(&wx);

    time_t now = time(nullptr);
    char time_str[32] = "--:--";
    if (now >= 1000000000LL) {
        struct tm ti;
        localtime_r(&now, &ti);
        weather_format_clock(time_str, sizeof(time_str), ti.tm_hour, ti.tm_min, ti.tm_sec, s.clock_24h, true);
    }

    JsonDocument doc;
    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["wifi_ssid"] = s.wifi_ssid[0] ? s.wifi_ssid : WiFi.SSID();
    settings["wifi_open"] = s.wifi_password[0] == '\0';
    settings["tz_preset"] = s.tz_preset;
    settings["tz_name"] = settings_get_tz_name(s.tz_preset);
    settings["tz_custom"] = s.tz_custom;
    settings["station_id"] = s.station_id;
    settings["api_token_saved"] = s.api_token[0] != '\0';
    settings["device_serial"] = s.device_serial;
    settings["rotation_supported"] = true;
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    settings["brightness_min"] = BACKLIGHT_MIN_ON;
#else
    settings["brightness_min"] = 0;
#endif
    settings["units"] = s.units;
    settings["clock_24h"] = s.clock_24h;
    settings["screen_rotation"] = s.screen_rotation;
    settings["brightness_day"] = s.brightness_day;
    settings["brightness_dim"] = s.brightness_dim;
    settings["dim_timeout_s"] = s.dim_timeout_s;
    settings["auto_scroll_s"] = s.auto_scroll_s;
    settings["startup_screen"] = s.startup_screen;
    settings["auto_scroll_mask"] = s.auto_scroll_mask;
    settings["night_wake_s"] = s.night_wake_s;
    settings["night_mode_enabled"] = s.night_mode_enabled;
    settings["night_start_hour"] = s.night_start_hour;
    settings["night_end_hour"] = s.night_end_hour;
    settings["brightness_night"] = s.brightness_night;

    JsonObject telemetry = doc["telemetry"].to<JsonObject>();
    telemetry["ip"] = WiFi.localIP().toString();
    telemetry["rssi"] = WiFi.RSSI();
    telemetry["local_time"] = time_str;
    telemetry["udp_received"] = wx.udp_connected;
    telemetry["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    telemetry["feed"] = tempest_feed_label(wx);
    uint32_t tick = millis();
    const WeatherWind reading = weather_wind(wx, tick, now);
    JsonObject wind = telemetry["wind"].to<JsonObject>();
    wind["source"] = reading.source;
    wind["unit"] = wind_unit_str((UnitSystem)s.units);
    if (reading.received) wind["age_s"] = reading.age_s;
    else wind["age_s"] = nullptr;
    if (isfinite(reading.speed_ms)) wind["speed"] = wind_to_unit(reading.speed_ms, (UnitSystem)s.units);
    else wind["speed"] = nullptr;
    wind["calm"] = isfinite(reading.speed_ms) && reading.speed_ms < 0.5f;
    if (reading.direction_valid) wind["direction_deg"] = reading.direction_deg;
    else wind["direction_deg"] = nullptr;
    const char *wind_keys[] = {"gust", "lull", "avg"};
    float wind_values[] = {wx.wind_gust_ms, wx.wind_lull_ms, wx.wind_avg_ms};
    for (int i = 0; i < 3; ++i) {
        if (reading.observation_fresh && isfinite(wind_values[i]) && wind_values[i] >= 0)
            wind[wind_keys[i]] = wind_to_unit(wind_values[i], (UnitSystem)s.units);
        else wind[wind_keys[i]] = nullptr;
    }
    telemetry["firmware_version"] = FW_VERSION;
    telemetry["board"] = BOARD_NAME;
    telemetry["chip"] = BOARD_CHIP_NAME;
    telemetry["reset_reason"] = weather_reset_label(esp_reset_reason());
    telemetry["heap_free"] = ESP.getFreeHeap();
    telemetry["heap_min_free"] = ESP.getMinFreeHeap();
    telemetry["heap_largest_block"] = ESP.getMaxAllocHeap();
    const WeatherRain rainfall = weather_rain(wx, tick, now);
    const WeatherEnvironment surroundings = weather_environment(wx, tick, now);
    JsonObject environment = telemetry["environment"].to<JsonObject>();
    environment["source"] = surroundings.source;
    environment["battery_source"] = surroundings.battery_source;
    if (surroundings.received) environment["age_s"] = surroundings.age_s;
    else environment["age_s"] = nullptr;
    if (surroundings.battery_received) environment["battery_age_s"] = surroundings.battery_age_s;
    else environment["battery_age_s"] = nullptr;
    if (isfinite(surroundings.uv)) environment["uv"] = surroundings.uv;
    else environment["uv"] = nullptr;
    if (isfinite(surroundings.solar)) environment["solar"] = surroundings.solar;
    else environment["solar"] = nullptr;
    if (isfinite(surroundings.battery)) environment["battery"] = surroundings.battery;
    else environment["battery"] = nullptr;
    JsonObject rain = telemetry["rain"].to<JsonObject>();
    rain["unit"] = rain_unit_str((UnitSystem)s.units);
    rain["rate_unit"] = rain_rate_unit_str((UnitSystem)s.units);
    rain["source"] = rainfall.source;
    rain["category"] = weather_rain_intensity(rainfall.intensity);
    rain["complete"] = rainfall.complete;
    if (rainfall.received) rain["age_s"] = rainfall.age_s;
    else rain["age_s"] = nullptr;
    if (isfinite(rainfall.rate_mm_hr)) rain["rate"] = rain_to_unit(rainfall.rate_mm_hr, (UnitSystem)s.units);
    else rain["rate"] = nullptr;
    if (isfinite(rainfall.today_mm)) rain["today"] = rain_to_unit(rainfall.today_mm, (UnitSystem)s.units);
    else rain["today"] = nullptr;
    if (isfinite(rainfall.yesterday_mm)) rain["yesterday"] = rain_to_unit(rainfall.yesterday_mm, (UnitSystem)s.units);
    else rain["yesterday"] = nullptr;
    if (rainfall.probability_pct >= 0) rain["probability_pct"] = rainfall.probability_pct;
    else rain["probability_pct"] = nullptr;
    telemetry["uptime_s"] = uint64_t(esp_timer_get_time() / 1000000LL);
    JsonObject lightning = telemetry["lightning"].to<JsonObject>();
    if (wx.has_udp_observation) {
        lightning["count_3h"] = wx.lightning_count_3h;
        lightning["count_today"] = wx.lightning_count_today;
    } else {
        lightning["count_3h"] = nullptr;
        lightning["count_today"] = nullptr;
    }
    lightning["source"] = !wx.has_udp_observation ? "Waiting for sensor observations" :
        weather_recent(true, tick, wx.last_udp_obs_millis, 90000) ? "Receiving sensor observations" : "Sensor observations stale; counts show received history";
    lightning["unit"] = dist_unit_str((UnitSystem)s.units);
    if (isfinite(wx.closest_strike_km) && wx.closest_strike_km >= 0)
        lightning["closest"] = dist_to_unit(wx.closest_strike_km, (UnitSystem)s.units);
    else lightning["closest"] = nullptr;
    lightning["event_received"] = wx.last_strike_epoch > 0;
    uint32_t strike_age;
    if (weather_event_age(wx.last_strike_epoch, now, &strike_age)) lightning["last_age_s"] = strike_age;
    else lightning["last_age_s"] = nullptr;
    telemetry["clock_synced"] = now >= 1000000000LL;
    telemetry["screen"] = ui_get_active_screen();
    const WeatherForecast outlook = weather_forecast(wx, tick);
    telemetry["forecast_fresh"] = outlook.fresh;
    JsonObject forecast_json = telemetry["forecast"].to<JsonObject>();
    forecast_json["source"] = outlook.source;
    forecast_json["conditions"] = outlook.conditions;
    forecast_json["unit"] = temp_unit_str((UnitSystem)s.units);
    if (outlook.received) forecast_json["age_s"] = outlook.age_s;
    else forecast_json["age_s"] = nullptr;
    const char *forecast_keys[] = {"high", "low", "feels"};
    float forecast_values[] = {outlook.high_c, outlook.low_c, outlook.feels_c};
    for (int i = 0; i < 3; ++i) {
        if (isfinite(forecast_values[i])) forecast_json[forecast_keys[i]] = temp_to_unit(forecast_values[i], (UnitSystem)s.units);
        else forecast_json[forecast_keys[i]] = nullptr;
    }
    bool configured = s.station_id && s.api_token[0];
    bool connected = WiFi.status() == WL_CONNECTED;
    bool clock_ready = now >= 1000000000LL;
    bool pending = tempest_rest_refresh_pending();
    JsonObject cloud = telemetry["cloud"].to<JsonObject>();
    cloud["configured"] = configured;
    cloud["busy"] = wx.cloud_busy;
    cloud["pending"] = pending;
    uint32_t remaining = weather_cloud_remaining(tick, wx.cloud_retry_started, wx.cloud_retry_interval);
    cloud["cooldown"] = configured && wx.cloud_service_cooldown && remaining > 0;
    cloud["state"] = !configured ? "Not configured" : !connected ? "Waiting for Wi-Fi" :
        !clock_ready ? "Waiting for clock" : wx.cloud_busy ? "Refreshing" :
        pending ? "Refresh queued" : "Waiting for next attempt";
    cloud["result"] = weather_cloud_result_label(wx.cloud_result);
    cloud["http_code"] = wx.cloud_http_code;
    if (wx.cloud_attempted) cloud["attempt_age_s"] = uint32_t(tick - wx.cloud_attempt_millis) / 1000;
    else cloud["attempt_age_s"] = nullptr;
    if (configured && connected && clock_ready && !wx.cloud_busy)
        cloud["retry_in_s"] = pending && !wx.cloud_service_cooldown ? 0 : (remaining + 999) / 1000;
    else cloud["retry_in_s"] = nullptr;
    if (wx.has_observation) telemetry["observation_age_s"] = weather_observation_age(wx, tick, now);
    else telemetry["observation_age_s"] = nullptr;
    if (wx.has_forecast) telemetry["forecast_age_s"] = uint32_t(tick - wx.last_forecast_millis) / 1000;
    else telemetry["forecast_age_s"] = nullptr;
    telemetry["pressure_unit"] = pressure_unit_str((UnitSystem)s.units);
    JsonArray history = telemetry["pressure_history"].to<JsonArray>();
    for (int i = 0; i < wx.pressure_hist_count; ++i) {
        JsonObject point = history.add<JsonObject>();
        point["value"] = pressure_to_unit(wx.pressure_hist_24h[i], (UnitSystem)s.units);
        point["epoch"] = wx.pressure_hist_epoch[i];
    }
    if (wx.pressure_hist_count) {
        int64_t sample = wx.pressure_hist_epoch[wx.pressure_hist_count - 1];
        telemetry["pressure_sample_age_s"] = now > sample ? uint64_t(now - sample) : 0;
    } else telemetry["pressure_sample_age_s"] = nullptr;

    const WeatherCurrent current = weather_current(wx, tick, now);
    JsonObject current_json = telemetry["current"].to<JsonObject>();
    current_json["source"] = current.source;
    current_json["temperature_unit"] = temp_unit_str((UnitSystem)s.units);
    current_json["pressure_unit"] = pressure_unit_str((UnitSystem)s.units);
    if (current.received) current_json["age_s"] = current.age_s;
    else current_json["age_s"] = nullptr;
    if (isfinite(current.temperature_c)) current_json["temperature"] = temp_to_unit(current.temperature_c, (UnitSystem)s.units);
    else current_json["temperature"] = nullptr;
    if (isfinite(current.humidity_pct)) current_json["humidity_pct"] = current.humidity_pct;
    else current_json["humidity_pct"] = nullptr;
    if (isfinite(current.dew_point_c)) current_json["dew_point"] = temp_to_unit(current.dew_point_c, (UnitSystem)s.units);
    else current_json["dew_point"] = nullptr;
    if (isfinite(current.pressure_mb)) current_json["pressure"] = pressure_to_unit(current.pressure_mb, (UnitSystem)s.units);
    else current_json["pressure"] = nullptr;
    float t_disp = temp_to_unit(current.temperature_c, (UnitSystem)s.units);
    char t_buf[24];
    snprintf(t_buf, sizeof(t_buf), "%.1f %s", t_disp, temp_unit_str((UnitSystem)s.units));
    if (!isfinite(t_disp)) strlcpy(t_buf, "—", sizeof(t_buf));
    telemetry["temp_display"] = t_buf;

    float p_disp = pressure_to_unit(current.pressure_mb, (UnitSystem)s.units);
    char p_buf[24];
    snprintf(p_buf, sizeof(p_buf), (s.units == 0 ? "%.2f inHg" : "%.1f mb"), p_disp);
    if (!isfinite(p_disp)) strlcpy(p_buf, "—", sizeof(p_buf));
    telemetry["press_display"] = p_buf;

    telemetry["conditions"] = outlook.conditions;
    telemetry["free_heap"] = ESP.getFreeHeap();
    telemetry["free_psram"] = ESP.getFreePsram();

    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
}

static void handle_api_settings() {
    if (!authorized()) return;
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    String body = s_server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parse error\"}");
        return;
    }

    if (!valid_settings_payload(doc)) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid setting value\"}");
        return;
    }

    AppSettings s;
    settings_get(&s);
    AppSettings previous = s;

    if (doc["wifi_ssid"].is<const char*>()) {
        const char *new_ssid = doc["wifi_ssid"].as<const char*>();
        if (strlen(new_ssid) > 0) {
            strncpy(s.wifi_ssid, new_ssid, sizeof(s.wifi_ssid) - 1);
            s.wifi_ssid[sizeof(s.wifi_ssid) - 1] = '\0';
        }
    }
    if (doc["wifi_pass"].is<const char*>()) {
        const char *new_pass = doc["wifi_pass"].as<const char*>();
        if (strlen(new_pass) > 0) {
            strncpy(s.wifi_password, new_pass, sizeof(s.wifi_password) - 1);
            s.wifi_password[sizeof(s.wifi_password) - 1] = '\0';
        }
    }
    if (doc["wifi_open"] == true) s.wifi_password[0] = '\0';

    if (doc["tz_preset"].is<uint8_t>()) s.tz_preset = doc["tz_preset"].as<uint8_t>();
    if (doc["tz_custom"].is<const char*>()) {
        strncpy(s.tz_custom, doc["tz_custom"].as<const char*>(), sizeof(s.tz_custom) - 1);
        s.tz_custom[sizeof(s.tz_custom) - 1] = '\0';
    }
    if (doc["station_id"].is<uint32_t>()) s.station_id = doc["station_id"].as<uint32_t>();
    if (doc["device_serial"].is<const char*>()) strlcpy(s.device_serial, doc["device_serial"], sizeof(s.device_serial));
    if (doc["api_token"].is<const char*>()) {
        if (strlen(doc["api_token"].as<const char*>()) > 0)
            strncpy(s.api_token, doc["api_token"].as<const char*>(), sizeof(s.api_token) - 1);
        s.api_token[sizeof(s.api_token) - 1] = '\0';
    }
    if (doc["clear_api_token"] == true) s.api_token[0] = '\0';
    if (doc["units"].is<uint8_t>()) s.units = doc["units"].as<uint8_t>();
    if (doc["clock_24h"].is<bool>()) s.clock_24h = doc["clock_24h"].as<bool>();
    if (doc["screen_rotation"].is<uint16_t>()) {
        uint16_t new_rot = doc["screen_rotation"].as<uint16_t>();
        if (new_rot != s.screen_rotation) {
            s.screen_rotation = new_rot;
            display::setRotation(new_rot);
        }
    }
    if (doc["brightness_day"].is<uint8_t>()) {
        s.brightness_day = doc["brightness_day"].as<uint8_t>();
        brightness_cancel_preview();
    }
    if (doc["brightness_dim"].is<uint8_t>()) s.brightness_dim = doc["brightness_dim"].as<uint8_t>();
    if (doc["dim_timeout_s"].is<uint16_t>()) s.dim_timeout_s = doc["dim_timeout_s"].as<uint16_t>();
    if (doc["auto_scroll_s"].is<uint16_t>()) s.auto_scroll_s = doc["auto_scroll_s"].as<uint16_t>();
    if (doc["startup_screen"].is<uint8_t>()) s.startup_screen = doc["startup_screen"].as<uint8_t>();
    if (doc["auto_scroll_mask"].is<uint8_t>()) s.auto_scroll_mask = doc["auto_scroll_mask"].as<uint8_t>();
    if (doc["night_wake_s"].is<uint16_t>()) s.night_wake_s = doc["night_wake_s"].as<uint16_t>();
    if (doc["night_mode_enabled"].is<bool>()) s.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
    if (doc["night_start_hour"].is<uint8_t>()) s.night_start_hour = doc["night_start_hour"].as<uint8_t>();
    if (doc["night_end_hour"].is<uint8_t>()) s.night_end_hour = doc["night_end_hour"].as<uint8_t>();
    if (doc["brightness_night"].is<uint8_t>()) s.brightness_night = doc["brightness_night"].as<uint8_t>();

    if (!settings_save(&s)) {
        s_server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Settings could not be saved\"}");
        return;
    }
    bool wifi_changed = strcmp(previous.wifi_ssid, s.wifi_ssid) || strcmp(previous.wifi_password, s.wifi_password);
    if (wifi_changed) {
        s_wifi_change_pending = true;
        s_wifi_change_ms = millis();
    }
    if (previous.station_id != s.station_id || strcmp(previous.device_serial, s.device_serial)) {
        tempest_state_init();
        tempest_set_units((UnitSystem)s.units);
    }

    // Trigger forecast fetch if station ID or API token changed
    if (previous.station_id != s.station_id || strcmp(previous.api_token, s.api_token)) tempest_rest_trigger_now();

    s_server.send(200, "application/json", wifi_changed ? "{\"status\":\"ok\",\"wifi_reconnecting\":true}" : "{\"status\":\"ok\"}");
}

static void handle_api_live() {
    if (!authorized()) return;
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_server.arg("plain"));
    if (err) {
        s_server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    if (!valid_settings_payload(doc)) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid setting value\"}");
        return;
    }

    AppSettings s;
    settings_get(&s);
    bool need_save = doc["save"].is<bool>() && doc["save"].as<bool>();

    // Live Brightness (Day)
    if (doc["brightness_day"].is<uint8_t>()) {
        uint8_t b = doc["brightness_day"].as<uint8_t>();
        s.brightness_day = b;
        if (need_save) brightness_cancel_preview();
        else brightness_preview(b);
    }
    if (doc["brightness_dim"].is<uint8_t>()) s.brightness_dim = doc["brightness_dim"].as<uint8_t>();
    if (doc["preview_cancel"] == true) brightness_cancel_preview();

    // Live Preview Dim (temporary brightness preview)
    if (doc["preview_dim"].is<uint8_t>()) {
        uint8_t b = doc["preview_dim"].as<uint8_t>();
        brightness_preview(b);
    }

    // Live Screen Rotation
    if (doc["screen_rotation"].is<uint16_t>()) {
        uint16_t new_rot = doc["screen_rotation"].as<uint16_t>();
        if (new_rot != s.screen_rotation) {
            s.screen_rotation = new_rot;
            display::setRotation(new_rot);
        }
    }

    // Live Units
    if (doc["clock_24h"].is<bool>()) s.clock_24h = doc["clock_24h"].as<bool>();
    if (doc["units"].is<uint8_t>()) {
        s.units = doc["units"].as<uint8_t>();
        tempest_set_units((UnitSystem)s.units);
    }

    // Live Auto-Scroll
    if (doc["startup_screen"].is<uint8_t>()) s.startup_screen = doc["startup_screen"].as<uint8_t>();
    if (doc["auto_scroll_mask"].is<uint8_t>()) s.auto_scroll_mask = doc["auto_scroll_mask"].as<uint8_t>();
    if (doc["night_wake_s"].is<uint16_t>()) s.night_wake_s = doc["night_wake_s"].as<uint16_t>();
    if (doc["auto_scroll_s"].is<uint16_t>()) {
        s.auto_scroll_s = doc["auto_scroll_s"].as<uint16_t>();
    }

    // Live Dim Timeout
    if (doc["dim_timeout_s"].is<uint16_t>()) {
        s.dim_timeout_s = doc["dim_timeout_s"].as<uint16_t>();
    }

    // Live Timezone
    if (doc["tz_preset"].is<uint8_t>()) {
        s.tz_preset = doc["tz_preset"].as<uint8_t>();
        need_save = true;
    }

    if (need_save && !settings_save(&s)) {
        s_server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Settings could not be saved\"}");
        return;
    }

    s_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_api_wifi_scan() {
    if (!authorized()) return;
    Serial.println("[web] Scanning available Wi-Fi networks...");
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true, false);
        s_server.send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    }
    if (n == WIFI_SCAN_RUNNING) {
        s_server.send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();

    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // skip hidden

            // Deduplicate SSIDs, keeping highest RSSI
            bool found = false;
            for (JsonObject item : arr) {
                if (ssid.equals(item["ssid"].as<const char*>())) {
                    found = true;
                    if (WiFi.RSSI(i) > item["rssi"].as<int>()) {
                        item["rssi"] = WiFi.RSSI(i);
                    }
                    break;
                }
            }
            if (!found) {
                JsonObject item = arr.add<JsonObject>();
                item["ssid"] = ssid;
                item["rssi"] = WiFi.RSSI(i);
                item["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
        }
    }
    WiFi.scanDelete();

    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
}

static void handle_api_forecast_refresh() {
    if (!authorized()) return;
    tempest_rest_trigger_now();
    s_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_view() {
    if (!authorized()) return;
    if (s_server.hasArg("i")) {
        String value = s_server.arg("i");
        if (value.length() != 1 || value[0] < '0' || value[0] > '4') {
            s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid screen\"}");
            return;
        }
        int idx = value[0] - '0';
        ui_set_screen(idx);
        s_server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        s_server.send(400, "text/plain", "Missing i parameter");
    }
}

static void handle_shot_bmp() {
    if (!authorized()) return;
    const uint16_t *fb = display::captureFrame();
    if (!fb) {
        s_server.send(503, "text/plain", "Capture buffer unavailable");
        return;
    }

    WiFiClient client = s_server.client();
    const uint32_t w = SCREEN_W;
    const uint32_t h = SCREEN_H;
    const uint32_t row_bytes = w * 3;
    const uint32_t pad = (4 - (row_bytes % 4)) % 4;
    const uint32_t image_size = (row_bytes + pad) * h;
    const uint32_t file_size = 54 + image_size;

    uint8_t hdr[54];
    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF; hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = image_size & 0xFF; hdr[35] = (image_size >> 8) & 0xFF; hdr[36] = (image_size >> 16) & 0xFF; hdr[37] = (image_size >> 24) & 0xFF;

    const size_t pixels = (size_t)w * h;
    uint16_t *frame = (uint16_t *)heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) frame = (uint16_t *)heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_8BIT);
    uint8_t *row_buf = frame ? (uint8_t *)heap_caps_malloc(row_bytes + pad, MALLOC_CAP_8BIT) : nullptr;
    if (!frame || !row_buf) {
        heap_caps_free(frame);
        heap_caps_free(row_buf);
        s_server.send(503, "text/plain", "Capture buffer unavailable");
        return;
    }
    memcpy(frame, fb, pixels * sizeof(uint16_t));
    memset(row_buf, 0, row_bytes + pad);
    s_server.setContentLength(file_size);
    s_server.send(200, "image/bmp", "");

    client.write(hdr, 54);

    for (int y = (int)h - 1; y >= 0; --y) {
        const uint16_t *src_row = &frame[y * w];
        uint8_t *dst = row_buf;
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t c = src_row[x];
            uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((c >> 5)  & 0x3F) * 255 / 63;
            uint8_t b = (c         & 0x1F) * 255 / 31;
            *dst++ = b;
            *dst++ = g;
            *dst++ = r;
        }
        client.write(row_buf, row_bytes + pad);
        if (((int)h - y) % 16 == 0) delay(0);
    }
    heap_caps_free(row_buf);
    heap_caps_free(frame);
}

static void handle_api_restart() {
    if (!authorized()) return;
    s_server.send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

static void ota_reset() {
    s_ota_started = s_ota_complete = s_ota_armed = s_ota_rejected = false;
    s_ota_header_ok = s_ota_marked = false;
    s_ota_prefix_len = s_ota_tail_len = 0;
}

static void ota_write(const uint8_t *data, size_t len) {
    if (!s_ota_armed || s_ota_rejected || !data || !len) return;
    if (!s_ota_header_ok) {
        size_t take = len < (112 - s_ota_prefix_len) ? len : (112 - s_ota_prefix_len);
        memcpy(s_ota_prefix + s_ota_prefix_len, data, take);
        s_ota_prefix_len += take;
        data += take;
        len -= take;
        if (s_ota_prefix_len < 112) return;
        if (!ota_image_acceptable(s_ota_prefix, 112, ota_expected_chip(), ota_expected_project())) {
            s_ota_rejected = true;
            Serial.println("[ota] Rejected image: chip or project does not match this display");
            return;
        }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Update.printError(Serial);
            s_ota_rejected = true;
            return;
        }
        s_ota_started = s_ota_header_ok = true;
        if (Update.write(s_ota_prefix, 112) != 112) {
            Update.printError(Serial);
            s_ota_rejected = true;
            return;
        }
        if (ota_note_marker(s_ota_tail, &s_ota_tail_len, s_ota_prefix, 112)) s_ota_marked = true;
    }
    if (len && Update.write((uint8_t *)data, len) != len) {
        Update.printError(Serial);
        s_ota_rejected = true;
        return;
    }
    if (len && ota_note_marker(s_ota_tail, &s_ota_tail_len, data, len)) s_ota_marked = true;
}

static void handle_ota_finish() {
    if (!authorized()) return;
    bool ok = s_ota_complete && !Update.hasError();
    if (ok) {
        s_server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update success. Rebooting...\"}");
    } else if (s_ota_rejected) {
        s_server.send(500, "application/json", "{\"status\":\"error\",\"message\":\"Firmware does not match this display\"}");
    } else {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "Update failed (Error #%u)", Update.getError());
        s_server.send(500, "application/json", String("{\"status\":\"error\",\"message\":\"") + err_msg + "\"}");
    }
    delay(500);
    if (ok) ESP.restart();
}

static void handle_ota_upload() {
    HTTPUpload& upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        ota_reset();
        if (!authorized(false)) return;
        s_ota_armed = true;
        Serial.printf("[ota] Receiving update file: '%s'\n", upload.filename.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        ota_write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!s_ota_armed) return;
        if (!s_ota_header_ok || s_ota_rejected || !s_ota_marked) {
            if (s_ota_started) Update.abort();
            s_ota_started = s_ota_complete = false;
            s_ota_rejected = true;
            Serial.println("[ota] Update rejected before it was committed");
            return;
        }
        if (Update.end(true)) {
            s_ota_complete = true;
            Serial.printf("[ota] Update successfully flashed: %u bytes written!\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (s_ota_started) Update.abort();
        s_ota_started = s_ota_complete = false;
        Serial.println("[ota] Update aborted by client");
    }
}

void web_config_server_network_changed() {
    // Start mDNS service responder: http://weather.local
    MDNS.end();
    if (WiFi.status() == WL_CONNECTED && MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.setInstanceName("Weather Station Display");
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "path", "/");
        Serial.printf("[web] mDNS responder started: http://%s.local\n", MDNS_HOSTNAME);
    }
}

void web_config_server_begin() {
    if (s_server_started) return;
    web_config_server_network_changed();

    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/api/status", HTTP_GET, handle_api_status);
    s_server.on("/api/settings", HTTP_POST, handle_api_settings);
    s_server.on("/api/live", HTTP_POST, handle_api_live);
    s_server.on("/api/wifi_scan", HTTP_GET, handle_api_wifi_scan);
    s_server.on("/api/forecast_refresh", HTTP_POST, handle_api_forecast_refresh);
    s_server.on("/api/restart", HTTP_POST, handle_api_restart);
    s_server.on("/api/ota_update", HTTP_POST, handle_ota_finish, handle_ota_upload);
    s_server.on("/shot.bmp", HTTP_GET, handle_shot_bmp);
    s_server.on("/view", HTTP_GET, handle_view);
    s_server.on("/view", HTTP_POST, handle_view);

    auto captive_root = []() {
        if (!s_setup_ap) { s_server.send(404, "text/plain", "Not found"); return; }
        handle_root();
    };
    s_server.on("/hotspot-detect.html", HTTP_GET, captive_root);
    s_server.on("/canonical.html", HTTP_GET, captive_root);
    s_server.on("/generate_204", HTTP_GET, captive_root);
    s_server.on("/gen_204", HTTP_GET, captive_root);
    s_server.on("/ncsi.txt", HTTP_GET, []() {
        if (!s_setup_ap) { s_server.send(404, "text/plain", "Not found"); return; }
        s_server.send(200, "text/plain", "Microsoft NCSI");
    });
    s_server.onNotFound([]() {
        if (!s_setup_ap) {
            s_server.send(404, "text/plain", "Not found");
            return;
        }
        s_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        s_server.send(302, "text/plain", "");
    });

    const char *headers[] = {"Origin"};
    s_server.collectHeaders(headers, 1);
    s_server.begin();
    s_server_started = true;

    Serial.printf("[web] Web configuration server active on port 80 (http://%s/)\n",
                  WiFi.localIP().toString().c_str());
}

void web_config_server_loop() {
    if (s_server_started) {
        s_server.handleClient();
    }
    if (s_wifi_change_pending && uint32_t(millis() - s_wifi_change_ms) >= 750) {
        s_wifi_change_pending = false;
        AppSettings s;
        settings_get(&s);
        WiFi.disconnect(false, false);
        WiFi.begin(s.wifi_ssid, s.wifi_password);
        network_credentials_changed();
    }
}

void web_config_server_set_setup_ap(bool up) { s_setup_ap = up; }
