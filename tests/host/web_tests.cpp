#include "Arduino.h"
#include "Preferences.h"
#include "tempest_state.h"
#include "settings_mgr.h"
#include "brightness_mgr.h"
#include <ArduinoJson.h>
#include "weather_cloud_schema.h"
#include "weather_ota.h"
#include <cassert>
#include <cstring>
#include <iostream>

uint32_t test_millis = 1000;
time_t test_epoch = 1791072000;
namespace display { void setBrightness(uint8_t) {} void setRotation(uint16_t) {} }
static int refreshes = 0;
void tempest_rest_trigger_now() { ++refreshes; }
static int selected_screen = -1;
void ui_set_screen(int idx) { selected_screen = idx; }
enum { DIGEST_AUTH, UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED, UPDATE_SIZE_UNKNOWN, U_FLASH };
struct HTTPUpload { int status; String filename; uint8_t *buf = nullptr; size_t currentSize = 0, totalSize = 0; };
struct FakeServer {
    bool allowed = true;
    String origin, body, response;
    int code = 0;
    HTTPUpload file;
    bool authenticate(const char *, const char *) { return allowed; }
    void requestAuthentication(int, const char *, const String &) { code = 401; }
    String header(const char *) { return origin; }
    String hostHeader() { return "weather.local"; }
    void sendHeader(const char *, const char *) {}
    bool hasArg(const char *) { return !body.empty(); }
    String arg(const char *) { return body; }
    void send(int status, const char *, const String &payload) { code = status; response = payload; }
    HTTPUpload &upload() { return file; }
} s_server;
struct FakeUpdate {
    bool begin_ok = true, error = false;
    int begins = 0, writes = 0, ends = 0, aborts = 0;
    bool begin(int, int) { ++begins; return begin_ok; }
    size_t write(uint8_t *, size_t size) { ++writes; return size; }
    bool end(bool = false) { ++ends; return !error; }
    bool hasError() { return error; }
    unsigned getError() { return error ? 1 : 0; }
    void printError(HostSerial &) {}
    void abort() { ++aborts; }
} Update;
struct { int restarts = 0; void restart() { ++restarts; } } ESP;
static bool s_wifi_change_pending = false, s_ota_started = false, s_ota_complete = false;
static bool s_ota_armed = false, s_ota_rejected = false, s_ota_header_ok = false, s_ota_marked = false;
static uint8_t s_ota_prefix[112];
static size_t s_ota_prefix_len = 0;
static uint8_t s_ota_tail[32];
static size_t s_ota_tail_len = 0;
static uint32_t s_wifi_change_ms = 0;
#include "web_handlers.inc"
#include "udp_observation.inc"

int main() {
    tempest_state_init();
    JsonDocument sensor;
    const char *valid_packet = R"({"obs":[[1791072000,1,3,5,180,3,1000,22,60,1000,0,0,0.25,1,-1,2,2.5,1]]})";
    assert(!deserializeJson(sensor, valid_packet));
    handle_obs_st(sensor);
    TempestState observed;
    tempest_get_state(&observed);
    assert(observed.has_observation && observed.air_temp_c == 22);
    assert(observed.uv_index == 0 && observed.solar_wm2 == 0 && observed.battery_v == 2.5);
    sensor["obs"][0][0] = ++test_epoch;
    sensor["obs"][0][7] = 23;
    sensor["obs"][0][10] = nullptr;
    sensor["obs"][0][11] = nullptr;
    sensor["obs"][0][16] = nullptr;
    handle_obs_st(sensor);
    tempest_get_state(&observed);
    assert(observed.air_temp_c == 23 && observed.rain_today_mm == 0.5f);
    assert(std::isnan(observed.uv_index) && std::isnan(observed.solar_wm2) && std::isnan(observed.battery_v));
    sensor["obs"][0][0] = ++test_epoch;
    sensor["obs"][0][10] = "0";
    sensor["obs"][0][11] = -1;
    sensor["obs"][0][16] = 0;
    handle_obs_st(sensor);
    tempest_get_state(&observed);
    assert(observed.last_obs_epoch == test_epoch && observed.rain_today_mm == 0.75f);
    assert(std::isnan(observed.uv_index) && std::isnan(observed.solar_wm2) && std::isnan(observed.battery_v));
    // Core fields remain strict, including integer counters and directions.
    for (int index : {0, 4, 7, 13, 15}) {
        JsonDocument invalid;
        assert(!deserializeJson(invalid, valid_packet));
        invalid["obs"][0][0] = test_epoch + 1;
        if (index == 7) invalid["obs"][0][index] = nullptr;
        else invalid["obs"][0][index] = 1.5;
        handle_obs_st(invalid);
        tempest_get_state(&observed);
        assert(observed.last_obs_epoch == test_epoch && observed.rain_today_mm == 0.75f);
    }
    JsonDocument short_packet;
    assert(!deserializeJson(short_packet, R"({"obs":[[1791072003,1,2]]})"));
    handle_obs_st(short_packet);
    tempest_get_state(&observed);
    assert(observed.last_obs_epoch == test_epoch);
    sensor["obs"][0][0] = ++test_epoch;
    sensor["obs"][0][8] = 0;
    handle_obs_st(sensor);
    tempest_get_state(&observed);
    assert(observed.humidity_pct == 0 && std::isnan(observed.dew_point_c));
    sensor["obs"][0][0] = ++test_epoch;
    sensor["obs"][0][8] = 100;
    handle_obs_st(sensor);
    tempest_get_state(&observed);
    assert(std::fabs(observed.dew_point_c - observed.air_temp_c) < 0.01);
    test_epoch = 1791072000;
    std::cout << "Actual UDP observation handler tests passed: optional values, zeroes, core validation and short packets\n";
    JsonDocument response;
    assert(!deserializeJson(response, R"({"current_conditions":{"time":1791072000,"air_temperature":22,"relative_humidity":60,"station_pressure":1000,"wind_avg":2,"conditions":"Clear"},"forecast":{"daily":[{}]}})"));
    JsonObjectConst current = response["current_conditions"].as<JsonObjectConst>();
    assert(weather_cloud_valid_current(current));
    assert(weather_cloud_valid_forecast(current, response["forecast"].as<JsonObjectConst>()));
    response["forecast"]["daily"] = nullptr;
    assert(!weather_cloud_valid_forecast(current, response["forecast"].as<JsonObjectConst>()));
    response["forecast"]["daily"].to<JsonArray>().add<JsonObject>();
    response["current_conditions"]["wind_direction"] = 999;
    assert(!weather_cloud_valid_current(current));
    response["current_conditions"].remove("wind_direction");
    assert(std::isnan(weather_cloud_number(response["forecast"]["daily"][0]["air_temp_high"])));
    response["forecast"]["daily"][0]["air_temp_high"] = "unknown";
    assert(std::isnan(weather_cloud_number(response["forecast"]["daily"][0]["air_temp_high"])));
    response["forecast"]["daily"][0]["air_temp_high"] = 25.5;
    assert(weather_cloud_number(response["forecast"]["daily"][0]["air_temp_high"]) == 25.5f);
    response["current_conditions"]["relative_humidity"] = 120;
    assert(!weather_cloud_valid_current(current));
    response["current_conditions"]["relative_humidity"] = 60;
    response["current_conditions"]["air_temperature"] = "22";
    assert(!weather_cloud_valid_current(current));
    response.remove("forecast");
    assert(!weather_cloud_valid_forecast(current, response["forecast"].as<JsonObjectConst>()));
    tempest_state_init();
    settings_init();
    s_server.body = R"({"brightness_dim":72,"save":true})";
    s_server.allowed = false;
    handle_api_live();
    assert(s_server.code == 401);
    AppSettings settings;
    settings_get(&settings);
    assert(settings.brightness_dim == 35);
    s_server.allowed = true;
    int refresh_count = refreshes;
    s_server.body = R"({"clock_24h":true})";
    handle_api_settings();
    settings_get(&settings);
    assert(s_server.code == 200 && settings.clock_24h && refreshes == refresh_count);
    s_server.body = R"({"clock_24h":"yes","save":true})";
    handle_api_live();
    assert(s_server.code == 400);
    s_server.body = R"({"clock_24h":false,"save":true})";
    handle_api_live();
    settings_get(&settings);
    assert(s_server.code == 200 && !settings.clock_24h);
    s_server.body = R"({"brightness_dim":72,"save":true})";
    s_server.allowed = true;
    s_server.body = "2";
    handle_view();
    assert(s_server.code == 200 && selected_screen == 2);
    s_server.body = "invalid";
    handle_view();
    assert(s_server.code == 400 && selected_screen == 2);
    s_server.body = R"({"brightness_dim":72,"save":true})";
    s_server.origin = "http://untrusted.example";
    handle_api_live();
    assert(s_server.code == 403);
    s_server.origin = "http://weather.local";
    handle_api_live();
    settings_get(&settings);
    assert(s_server.code == 200 && settings.brightness_dim == 72 && test_preferences["b_dim"] == "72");
    s_server.body = R"({"units":9,"save":true})";
    handle_api_live();
    assert(s_server.code == 400 && tempest_get_units() == UNIT_IMPERIAL);
    s_server.body = R"({"screen_rotation":45,"save":true})";
    handle_api_live();
    assert(s_server.code == 400);
    s_server.body = R"({"night_start_hour":24})";
    handle_api_settings();
    assert(s_server.code == 400);
    s_server.body = R"({"startup_screen":3,"auto_scroll_mask":5,"night_wake_s":60,"save":true})";
    handle_api_live();
    settings_get(&settings);
    assert(s_server.code == 200 && settings.startup_screen == 3 && settings.auto_scroll_mask == 5 && settings.night_wake_s == 60);
    s_server.body = R"({"startup_screen":5})";
    handle_api_settings();
    assert(s_server.code == 400);
    s_server.body = R"({"auto_scroll_mask":16})";
    handle_api_live();
    assert(s_server.code == 400);
    s_server.body = R"({"night_wake_s":301})";
    handle_api_live();
    assert(s_server.code == 400);
    s_server.body = R"({"startup_screen":4,"auto_scroll_mask":0,"night_wake_s":0})";
    handle_api_settings();
    settings_get(&settings);
    assert(s_server.code == 200 && settings.startup_screen == 4 && settings.auto_scroll_mask == 0 && settings.night_wake_s == 0);
    strcpy(settings.api_token, "saved-token");
    settings_save(&settings);
    s_server.body = R"({"api_token":"","wifi_ssid":"new-network","wifi_pass":"new-password"})";
    handle_api_settings();
    settings_get(&settings);
    assert(s_server.code == 200 && s_wifi_change_pending && !strcmp(settings.api_token, "saved-token"));
    s_server.body = R"({"wifi_open":true,"clear_api_token":true})";
    handle_api_settings();
    settings_get(&settings);
    assert(s_server.code == 200 && settings.wifi_password[0] == 0 && settings.api_token[0] == 0);
    assert(test_preferences.find("token") == test_preferences.end());
    assert(test_preferences.find("wifi_pass") == test_preferences.end());
    uint8_t image[160];
    auto fill_image = [&](uint16_t chip, const char *project, bool marker) {
        memset(image, 0, sizeof(image));
        image[0] = 0xE9;
        image[12] = (uint8_t)chip;
        image[13] = (uint8_t)(chip >> 8);
        image[32] = 0x32; image[33] = 0x54; image[34] = 0xCD; image[35] = 0xAB;
        memcpy(image + 80, project, strlen(project));
        if (marker) memcpy(image + 120, ota_image_marker(), strlen(ota_image_marker()));
    };
    s_server.file.buf = image;
    s_server.file.currentSize = sizeof(image);
    s_server.file.status = UPLOAD_FILE_START;
    s_server.allowed = false;
    handle_ota_upload();
    assert(Update.begins == 0);
    handle_ota_finish();
    assert(s_server.code == 401 && ESP.restarts == 0);
    s_server.allowed = true;
    handle_ota_finish(); // no armed upload is not success
    assert(s_server.code == 500 && ESP.restarts == 0);
    s_server.file.status = UPLOAD_FILE_START;
    handle_ota_upload();
    fill_image(9, "arduino-lib-builder", true);
    s_server.file.status = UPLOAD_FILE_WRITE;
    handle_ota_upload();
    assert(Update.begins == 1);
    s_server.file.status = UPLOAD_FILE_ABORTED;
    handle_ota_upload();
    handle_ota_finish();
    assert(Update.aborts == 1 && s_server.code == 500 && ESP.restarts == 0);
    s_server.file.status = UPLOAD_FILE_START;
    handle_ota_upload();
    fill_image(18, "arduino-lib-builder", true);
    s_server.file.status = UPLOAD_FILE_WRITE;
    handle_ota_upload();
    s_server.file.status = UPLOAD_FILE_END;
    handle_ota_upload();
    handle_ota_finish();
    assert(s_server.code == 500 && ESP.restarts == 0 && Update.begins == 1);
    assert(s_server.response.find("does not match") != std::string::npos);
    s_server.file.status = UPLOAD_FILE_START;
    handle_ota_upload();
    fill_image(9, "other-project", true);
    s_server.file.status = UPLOAD_FILE_WRITE;
    handle_ota_upload();
    s_server.file.status = UPLOAD_FILE_END;
    handle_ota_upload();
    handle_ota_finish();
    assert(s_server.code == 500 && ESP.restarts == 0 && Update.begins == 1);
    s_server.file.status = UPLOAD_FILE_START;
    handle_ota_upload();
    fill_image(9, "arduino-lib-builder", false);
    s_server.file.status = UPLOAD_FILE_WRITE;
    handle_ota_upload();
    s_server.file.status = UPLOAD_FILE_END;
    handle_ota_upload();
    handle_ota_finish();
    assert(s_server.code == 500 && ESP.restarts == 0 && Update.begins == 2 && Update.aborts == 2);
    s_server.file.status = UPLOAD_FILE_START;
    handle_ota_upload();
    fill_image(9, "arduino-lib-builder", true);
    s_server.file.status = UPLOAD_FILE_WRITE;
    s_server.file.currentSize = 100;
    handle_ota_upload();
    assert(Update.begins == 2);
    s_server.file.currentSize = sizeof(image) - 100;
    s_server.file.buf = image + 100;
    handle_ota_upload();
    assert(Update.begins == 3);
    s_server.file.status = UPLOAD_FILE_END;
    handle_ota_upload();
    handle_ota_finish();
    assert(s_server.code == 200 && ESP.restarts == 1);
    std::cout << "Actual web handler tests passed: auth, origin, validation, dim save, Wi-Fi change, token retention, OTA identity\n";
}
