#include "tempest_udp.h"
#include "tempest_state.h"
#include "config.h"
#include "settings_mgr.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#define UDP_RX_BUF_SIZE 2048

static TaskHandle_t s_udp_task_handle = nullptr;
static uint32_t s_stack_free = 0;

uint32_t tempest_udp_stack_free() { return s_stack_free; }

static void handle_rapid_wind(JsonDocument &doc) {
    JsonArray ob = doc["ob"];
    if (ob.size() >= 3) {
        if (!ob[0].is<int64_t>() || !ob[1].is<float>() || !ob[2].is<int>()) return;
        int64_t epoch = ob[0].as<int64_t>();
        float speed_ms = ob[1].as<float>();
        int dir_deg = ob[2].as<int>();
        tempest_update_rapid_wind(speed_ms, dir_deg, epoch);
    }
}

static void handle_obs_st(JsonDocument &doc) {
    JsonArray obs = doc["obs"];
    if (obs.size() < 1) return;
    JsonArray o = obs[0];
    if (o.size() < 18) return;
    const int numeric[] = {1, 2, 3, 6, 7, 8, 12, 14, 17};
    for (int index : numeric) if (!o[index].is<float>()) return;
    if (!o[0].is<int64_t>() || !o[4].is<int>() || !o[13].is<int>() || !o[15].is<int>()) return;
    if (o[13].as<int>() < 0 || o[13].as<int>() > 3) return;
    // Optional measurements must not discard the remaining observation or
    // become fabricated zeroes through coercion.
    const auto optional_number = [](JsonVariantConst value, bool positive = false) -> float {
        if (!value.is<float>()) return NAN;
        float number = value.as<float>();
        return isfinite(number) && (positive ? number > 0 : number >= 0) ? number : NAN;
    };

    int64_t epoch       = o[0].as<int64_t>();
    float   wind_lull   = o[1].as<float>();
    float   wind_avg    = o[2].as<float>();
    float   wind_gust   = o[3].as<float>();
    int     wind_dir    = o[4].as<int>();
    float   pressure    = o[6].as<float>();
    float   temp_c      = o[7].as<float>();
    float   humidity    = o[8].as<float>();
    float   uv          = optional_number(o[10]);
    float   solar       = optional_number(o[11]);
    float   rain_min    = o[12].as<float>();
    int     precip_type = o[13].as<int>();
    float   strike_dist = o[14].as<float>();
    int     strike_cnt  = o[15].as<int>();
    float   battery     = optional_number(o[16], true);

    Serial.printf("[tempest_udp] obs_st: %.1f C, %.0f%% RH, %.1f mb, wind %.1f m/s, rain %.2f mm (type %d)\n",
                  temp_c, humidity, pressure, wind_avg, rain_min, precip_type);

    tempest_update_obs(temp_c, humidity, pressure,
                       wind_avg, wind_gust, wind_lull, wind_dir,
                       uv, solar, rain_min, precip_type,
                       strike_dist, strike_cnt, battery, epoch);
}

static void handle_precip(JsonDocument &doc) {
    JsonArray evt = doc["evt"];
    if (evt.size() >= 1) {
        if (!evt[0].is<int64_t>()) return;
        int64_t epoch = evt[0].as<int64_t>();
        Serial.printf("[tempest_udp] Rain event started! Epoch: %lld\n", (long long)epoch);
        tempest_update_precip_event(epoch);
    }
}

static void handle_strike(JsonDocument &doc) {
    JsonArray evt = doc["evt"];
    if (evt.size() >= 3) {
        if (!evt[0].is<int64_t>() || !evt[1].is<float>() || !evt[2].is<uint32_t>()) return;
        int64_t  epoch   = evt[0].as<int64_t>();
        float    dist_km = evt[1].as<float>();
        uint32_t energy  = evt[2].as<uint32_t>();
        Serial.printf("[tempest_udp] Lightning strike! Distance: %.1f km\n", dist_km);
        tempest_update_strike(dist_km, energy, epoch);
    }
}

static void tempest_udp_task(void *pvParameters) {
    (void)pvParameters;
    char *rx_buf = (char *)malloc(UDP_RX_BUF_SIZE);
    if (!rx_buf) {
        Serial.println("[tempest_udp] Failed to allocate UDP buffer");
        s_udp_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    Serial.printf("[tempest_udp] Initializing BSD socket on UDP port %d...\n", TEMPEST_UDP_PORT);

    JsonDocument doc;

    while (true) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(TEMPEST_UDP_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            Serial.printf("[tempest_udp] socket() failed: errno %d\n", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        // SO_BROADCAST is critical for receiving 255.255.255.255 Tempest hub broadcasts
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            Serial.printf("[tempest_udp] bind(:%d) failed: errno %d\n", TEMPEST_UDP_PORT, errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        Serial.printf("[tempest_udp] Listening for Tempest UDP broadcasts on port %d...\n", TEMPEST_UDP_PORT);

        while (WiFi.status() == WL_CONNECTED) {
            struct sockaddr_storage src;
            socklen_t srclen = sizeof(src);
            int len = recvfrom(sock, rx_buf, UDP_RX_BUF_SIZE - 1, 0, (struct sockaddr *)&src, &srclen);
            if (len > 0) {
                rx_buf[len] = '\0';
                doc.clear();
                DeserializationError err = deserializeJson(doc, rx_buf);
                if (!err) {
                    AppSettings settings;
                    settings_get(&settings);
                    const char *serial = doc["serial_number"] | "";
                    if (settings.device_serial[0] && strcmp(settings.device_serial, serial)) continue;
                    const char *type = doc["type"];
                    if (type) {
                        if (strcmp(type, "rapid_wind") == 0) {
                            handle_rapid_wind(doc);
                        } else if (strcmp(type, "obs_st") == 0) {
                            handle_obs_st(doc);
                        } else if (strcmp(type, "evt_strike") == 0) {
                            handle_strike(doc);
                        } else if (strcmp(type, "evt_precip") == 0) {
                            handle_precip(doc);
                        }
                    }
                }
            } else if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    s_stack_free = uxTaskGetStackHighWaterMark(nullptr);
                    continue;
                }
                Serial.printf("[tempest_udp] recvfrom errno %d\n", errno);
                break;
            }
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    free(rx_buf);
    vTaskDelete(NULL);
}

void tempest_udp_start() {
    if (s_udp_task_handle) return;
    if (xTaskCreatePinnedToCore(
        tempest_udp_task,
        "tempest_udp",
        4096,
        NULL,
        2,
        &s_udp_task_handle,
        0
    ) != pdPASS) {
        s_udp_task_handle = nullptr;
        Serial.println("[tempest_udp] Task creation failed");
    }
}
