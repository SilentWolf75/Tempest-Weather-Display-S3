#include "tempest_rest.h"
#include "tempest_state.h"
#include "settings_mgr.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static TaskHandle_t s_rest_task_handle = nullptr;
static volatile bool s_force_fetch = false;

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

static void fetch_forecast() {
    if (WiFi.status() != WL_CONNECTED) return;

    AppSettings settings;
    settings_get(&settings);

    WiFiClientSecure client;
    client.setInsecure();

    char url[384];
    snprintf(url, sizeof(url),
             "https://swd.weatherflow.com/swd/rest/better_forecast?station_id=%u&units_temp=c&units_wind=mps&units_pressure=mb&units_precip=mm&units_distance=km&token=%s",
             settings.station_id, settings.api_token);

    HTTPClient http;
    http.useHTTP10(true); // forces HTTP/1.0 to eliminate chunked framing issues
    http.setReuse(false);
    http.setTimeout(12000);

    if (!http.begin(client, url)) {
        Serial.println("[tempest_rest] HTTP begin failed");
        client.stop();
        return;
    }

    Serial.println("[tempest_rest] Querying Better Forecast API...");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        PsramStream psramStream(131072); // 128 KB buffer in PSRAM
        if (!psramStream.isOk()) {
            Serial.println("[tempest_rest] Failed to allocate PSRAM stream");
            http.end();
            client.stop();
            return;
        }

        http.writeToStream(&psramStream);
        http.end();
        client.stop();

        JsonDocument doc(&s_json_psram);
        DeserializationError err = deserializeJson(doc, psramStream);

        if (!err) {
            JsonObjectConst current = doc["current_conditions"].as<JsonObjectConst>();
            JsonObjectConst forecast = doc["forecast"].as<JsonObjectConst>();

            const char *cond = current["conditions"] | "Clear";
            const char *icon = current["icon"] | "clear-day";
            float feels_like = current["feels_like"] | 20.0f;
            float current_temp = current["air_temperature"] | 20.0f;
            float current_hum  = current["relative_humidity"] | 50.0f;
            float current_press = current["station_pressure"] | 1013.0f;
            float current_wind_avg = current["wind_avg"] | 0.0f;
            float current_wind_gust = current["wind_gust"] | 0.0f;
            int   current_wind_dir  = current["wind_direction"] | 0;
            float current_uv = current["uv"] | 0.0f;
            float current_solar = current["solar_radiation"] | 0.0f;
            int64_t current_time = current["time"] | 0;

            float high_c = 25.0f;
            float low_c  = 15.0f;

            JsonArrayConst daily = forecast["daily"].as<JsonArrayConst>();
            if (daily.size() > 0) {
                high_c = daily[0]["air_temp_high"] | current_temp;
                low_c  = daily[0]["air_temp_low"] | current_temp;
            }

            Serial.printf("[tempest_rest] Success! Conditions: '%s', Icon: '%s', Temp: %.1fC (%.0fF), High: %.1fC, Low: %.1fC\n",
                          cond, icon, current_temp, (current_temp * 1.8f + 32.0f), high_c, low_c);

            // Update forecast
            tempest_update_forecast(cond, icon, high_c, low_c, feels_like);

            // If no UDP packet has arrived yet, seed live observations from current conditions
            TempestState current_state;
            tempest_get_state(&current_state);
            if (!current_state.udp_connected) {
                tempest_update_obs(current_temp, current_hum, current_press,
                                  current_wind_avg, current_wind_gust, 0.0f, current_wind_dir,
                                  current_uv, current_solar, 0.0f,
                                  -1.0f, 0, 0.0f, current_time);
            }
        } else {
            Serial.printf("[tempest_rest] JSON parse failed: %s\n", err.c_str());
        }
    } else {
        Serial.printf("[tempest_rest] HTTP GET failed: code %d\n", httpCode);
        http.end();
        client.stop();
    }
}

static void tempest_rest_task(void *pvParameters) {
    (void)pvParameters;

    // Initial delay for WiFi and NTP to settle
    vTaskDelay(pdMS_TO_TICKS(4000));

    while (true) {
        fetch_forecast();

        // Wait for next cycle or force fetch
        uint32_t start_ms = millis();
        while (millis() - start_ms < TEMPEST_FORECAST_INTERVAL_MS) {
            if (s_force_fetch) {
                s_force_fetch = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void tempest_rest_start() {
    xTaskCreatePinnedToCore(
        tempest_rest_task,
        "tempest_rest",
        8192,
        NULL,
        1,
        &s_rest_task_handle,
        0
    );
}
