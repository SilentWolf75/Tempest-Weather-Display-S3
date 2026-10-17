#include "tempest_rest.h"
#include "tempest_state.h"
#include "settings_mgr.h"
#include "config.h"
#include "weather_cloud_schema.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <math.h>
#include <time.h>

static TaskHandle_t s_rest_task_handle = nullptr;
static uint32_t s_stack_free = 0;

uint32_t tempest_rest_stack_free() { return s_stack_free; }
static std::atomic<bool> s_force_fetch{false};
extern const char weatherflow_root[] asm("_binary_include_weatherflow_root_pem_start");

struct PsramJsonAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static PsramJsonAllocator s_json_psram;

class PsramStream : public Stream {
private:
    uint8_t* _buffer;
    size_t _capacity;
    size_t _writePos;
    size_t _readPos;

public:
    PsramStream(size_t capacity) {
        _capacity = capacity;
        _buffer = (uint8_t*)heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
        _writePos = 0;
        _readPos = 0;
    }

    ~PsramStream() {
        if (_buffer) heap_caps_free(_buffer);
    }

    bool isOk() const { return _buffer != nullptr; }

    size_t write(uint8_t c) override {
        if (_writePos < _capacity) {
            _buffer[_writePos++] = c;
            return 1;
        }
        return 0;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (!_buffer) return 0;
        size_t space = _capacity - _writePos;
        size_t toWrite = (size < space) ? size : space;
        memcpy(_buffer + _writePos, buffer, toWrite);
        _writePos += toWrite;
        return toWrite;
    }

    int read() override {
        if (_readPos < _writePos) {
            return _buffer[_readPos++];
        }
        return -1;
    }

    int peek() override {
        if (_readPos < _writePos) {
            return _buffer[_readPos];
        }
        return -1;
    }

    int available() override {
        return (int)(_writePos - _readPos);
    }
};

void tempest_rest_trigger_now() {
    s_force_fetch = true;
}

bool tempest_rest_refresh_pending() { return s_force_fetch.load(); }

struct FetchOutcome {
    CloudResult result;
    int http_code = 0;
    uint32_t retry_after_ms = 0;
};

static FetchOutcome fetch_forecast(const AppSettings &settings) {

    WiFiClientSecure client;
    client.setCACert(weatherflow_root);
    client.setHandshakeTimeout(12);

    char url[384];
    snprintf(url, sizeof(url),
             "https://swd.weatherflow.com/swd/rest/better_forecast?station_id=%u&units_temp=c&units_wind=mps&units_pressure=mb&units_precip=mm&units_distance=km&token=%s",
             settings.station_id, settings.api_token);

    HTTPClient http;
    http.useHTTP10(true); // forces HTTP/1.0 to eliminate chunked framing issues
    http.setReuse(false);
    http.setTimeout(12000);
    http.setConnectTimeout(12000);
    const char *headers[] = {"Retry-After"};
    http.collectHeaders(headers, 1);

    if (!http.begin(client, url)) {
        Serial.println("[tempest_rest] HTTP begin failed");
        client.stop();
        return {CloudResult::TransportError};
    }

    Serial.println("[tempest_rest] Querying Better Forecast API...");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        PsramStream psramStream(131072); // 128 KB buffer in PSRAM
        if (!psramStream.isOk()) {
            Serial.println("[tempest_rest] Failed to allocate PSRAM stream");
            http.end();
            client.stop();
            return {CloudResult::MemoryError, httpCode};
        }

        int written = http.writeToStream(&psramStream);
        http.end();
        client.stop();

        if (written <= 0 || written >= 131072) {
            Serial.println("[tempest_rest] Empty, truncated, or oversized response");
            return {CloudResult::InvalidResponse, httpCode};
        }
        JsonDocument doc(&s_json_psram);
        DeserializationError err = deserializeJson(doc, psramStream);

        if (!err) {
            AppSettings active;
            settings_get(&active);
            if (active.station_id != settings.station_id || strcmp(active.api_token, settings.api_token))
                return {CloudResult::SettingsChanged, httpCode};
            JsonObjectConst current = doc["current_conditions"].as<JsonObjectConst>();
            JsonObjectConst forecast = doc["forecast"].as<JsonObjectConst>();
            if (!weather_cloud_valid_current(current)) {
                Serial.println("[tempest_rest] Missing required observation fields");
                return {CloudResult::InvalidResponse, httpCode};
            }

            const char *cond = current["conditions"] | "";
            const char *icon = current["icon"] | "";
            float feels_like = weather_cloud_number(current["feels_like"]);
            float current_temp = current["air_temperature"].as<float>();
            float current_hum  = current["relative_humidity"].as<float>();
            float current_press = current["station_pressure"].as<float>();
            float current_wind_avg = weather_cloud_number(current["wind_avg"]);
            float current_wind_gust = weather_cloud_number(current["wind_gust"]);
            int current_wind_dir = -1;
            if (current["wind_direction"].is<int>()) current_wind_dir = current["wind_direction"].as<int>();
            float current_uv = weather_cloud_number(current["uv"]);
            float current_solar = weather_cloud_number(current["solar_radiation"]);
            int64_t current_time = current["time"] | 0;

            float high_c = NAN;
            float low_c  = NAN;

            JsonArrayConst daily = forecast["daily"].as<JsonArrayConst>();
            if (daily.size() > 0) {
                high_c = weather_cloud_number(daily[0]["air_temp_high"]);
                low_c  = weather_cloud_number(daily[0]["air_temp_low"]);
            }

            Serial.printf("[tempest_rest] Observation received. Conditions: '%s', Icon: '%s', Temp: %.1fC (%.0fF), High: %.1fC, Low: %.1fC\n",
                          cond, icon, current_temp, (current_temp * 1.8f + 32.0f), high_c, low_c);

            // Update forecast
            bool valid_forecast = weather_cloud_valid_forecast(current, forecast);
            if (valid_forecast) tempest_update_forecast(cond, icon, high_c, low_c, feels_like);

            // Update precipitation accumulators and probability
            float rain_today = current["precip_accum_local_day"] | -1.0f;
            float rain_yesterday = current["precip_accum_local_yesterday"] | -1.0f;
            int rain_prob = current["precip_probability"] | -1;
            if (rain_prob < 0 && daily.size() > 0) {
                rain_prob = daily[0]["precip_probability"] | -1;
            }
            tempest_update_rain_totals(rain_today, rain_yesterday, rain_prob, current_time);

            // The state layer prefers fresh UDP and accepts cloud observations during a gap.
            tempest_update_obs(current_temp, current_hum, current_press,
                                  current_wind_avg, current_wind_gust, NAN, current_wind_dir,
                                  current_uv, current_solar, 0.0f, 0,
                                  -1.0f, 0, 0.0f, current_time, false);
            return {valid_forecast ? CloudResult::Success : CloudResult::InvalidResponse, httpCode};
        } else {
            Serial.printf("[tempest_rest] JSON parse failed: %s\n", err.c_str());
            return {err == DeserializationError::NoMemory ? CloudResult::MemoryError : CloudResult::InvalidResponse, httpCode};
        }
    } else {
        Serial.printf("[tempest_rest] HTTP GET failed: code %d\n", httpCode);
        uint32_t retry_after = weather_cloud_retry_after(http.header("Retry-After").c_str());
        http.end();
        client.stop();
        return {weather_cloud_http_result(httpCode), httpCode, retry_after};
    }
}

static void tempest_rest_task(void *pvParameters) {
    (void)pvParameters;

    // Initial delay for WiFi and NTP to settle
    vTaskDelay(pdMS_TO_TICKS(4000));
    unsigned failures = 0;
    uint32_t wait_started = millis(), interval = 0;
    bool service_cooldown = false;
    uint32_t station = 0;
    char token[128] = {};

    while (true) {
        AppSettings settings;
        settings_get(&settings);
        if (settings.station_id != station || strcmp(settings.api_token, token)) {
            station = settings.station_id;
            strlcpy(token, settings.api_token, sizeof(token));
            failures = 0;
            interval = 0;
            service_cooldown = false;
        }
        s_stack_free = uxTaskGetStackHighWaterMark(nullptr);
        if (!settings.station_id || !settings.api_token[0]) {
            s_force_fetch.exchange(false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        // Keep waiting without reporting network/clock prerequisites as HTTP failures.
        if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1000000000LL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!weather_cloud_due(millis(), wait_started, interval, s_force_fetch.load(), service_cooldown)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        s_force_fetch.exchange(false);
        tempest_cloud_attempt();
        FetchOutcome outcome = fetch_forecast(settings);
        interval = weather_cloud_retry_delay(outcome.result, failures, outcome.retry_after_ms, TEMPEST_FORECAST_INTERVAL_MS);
        service_cooldown = outcome.result == CloudResult::RateLimited || outcome.retry_after_ms > 0;
        wait_started = millis();
        tempest_cloud_finish(outcome.result, outcome.http_code, interval, service_cooldown);
    }
}

void tempest_rest_start() {
    if (s_rest_task_handle) return;
    if (xTaskCreatePinnedToCore(
        tempest_rest_task,
        "tempest_rest",
        8192,
        NULL,
        1,
        &s_rest_task_handle,
        0
    ) != pdPASS) {
        s_rest_task_handle = nullptr;
        Serial.println("[tempest_rest] Task creation failed");
    }
}
