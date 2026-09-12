#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#if BOARD_HAS_WIFIMANAGER
#include <WiFiManager.h>
#endif
#include "settings_mgr.h"
#include "web_config_server.h"
#include "display.h"
#include "tempest_state.h"
#include "tempest_udp.h"
#include "tempest_rest.h"
#include "ui/ui.h"

static uint32_t s_last_ui_update_ms = 0;
static bool     s_is_dimmed = false;

#if BOARD_HAS_WIFIMANAGER
static WiFiManager s_wm;
#else
#include <DNSServer.h>
static DNSServer s_dns_server;
static bool     s_setup_ap_up = false;
static uint32_t s_sta_wait_ms = 0;
#define WIFI_PORTAL_AFTER_MS 45000UL

static void wifi_portal_tick() {
    if (WiFi.status() == WL_CONNECTED) {
        if (s_setup_ap_up) {
            Serial.println("[wifi] Station associated -> shutting down SoftAP portal");
            s_dns_server.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            s_setup_ap_up = false;
        }
        s_sta_wait_ms = millis();
        return;
    }

    if (s_setup_ap_up) {
        s_dns_server.processNextRequest();
        return;
    }

    if (s_sta_wait_ms == 0) { s_sta_wait_ms = millis(); return; }
    if (millis() - s_sta_wait_ms < WIFI_PORTAL_AFTER_MS) return;

    Serial.printf("[wifi] No association after %lus -> raising setup AP '%s'\n",
                  (unsigned long)(WIFI_PORTAL_AFTER_MS / 1000), AP_NAME);
    s_setup_ap_up = WiFi.mode(WIFI_AP_STA) && WiFi.softAP(AP_NAME);
    if (!s_setup_ap_up) s_setup_ap_up = WiFi.mode(WIFI_AP) && WiFi.softAP(AP_NAME);
    if (s_setup_ap_up) {
        s_dns_server.start(53, "*", WiFi.softAPIP());
        Serial.printf("[wifi] Setup AP active: connect to '%s', open http://%s/\n",
                      AP_NAME, WiFi.softAPIP().toString().c_str());
        web_config_server_begin();
    }
}
#endif

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n=========================================\n");
    Serial.printf("  %s v%s\n", FW_NAME, FW_VERSION);
    Serial.printf("  %s\n", BOARD_NAME);
    Serial.printf("=========================================\n");

    // Initialize persisted NVS settings & thread-safe weather state
    settings_init();
    tempest_state_init();

    // Bring up display and touch controller
    if (!display::begin()) {
        Serial.println("[main] FATAL: Display initialization failed!");
        while (1) { delay(1000); }
    }

    // Build the 5-screen LVGL UI
    ui_init();

    // Initial UI render pass
    ui_update();
    display::loop();

    // Wi-Fi configuration: Set hostname and auto-reconnect
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_HOSTNAME);
    WiFi.setAutoReconnect(true);

    AppSettings settings;
    settings_get(&settings);

    bool connected = false;
    if (strlen(settings.wifi_ssid) > 0) {
        Serial.printf("[main] Connecting to saved Wi-Fi '%s'...\n", settings.wifi_ssid);
        if (strlen(settings.wifi_password) > 0) {
            WiFi.begin(settings.wifi_ssid, settings.wifi_password);
        } else {
            // If no password saved in settings, let ESP-IDF use stored flash credentials
            WiFi.begin(settings.wifi_ssid);
        }
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(250);
            Serial.print(".");
            display::loop(); // Keep UI rendering active during connect
        }
        Serial.println();
        connected = (WiFi.status() == WL_CONNECTED);
    }

#if BOARD_HAS_WIFIMANAGER
    if (!connected) {
        Serial.println("[main] Saved Wi-Fi not connected yet. Starting WiFiManager fallback...");
        s_wm.setConfigPortalTimeout(120);
        s_wm.setConnectTimeout(20);
        connected = s_wm.autoConnect(AP_NAME);
    }
#else
    if (!connected) {
        Serial.printf("[main] Saved Wi-Fi not connected. Raising Setup SoftAP '%s'...\n", AP_NAME);
        WiFi.mode(WIFI_AP_STA);
        if (WiFi.softAP(AP_NAME)) {
            s_setup_ap_up = true;
            s_dns_server.start(53, "*", WiFi.softAPIP());
            Serial.printf("[main] Setup Hotspot active at http://%s/\n", WiFi.softAPIP().toString().c_str());
            web_config_server_begin();
        }
    }
#endif

    if (connected) {
        Serial.printf("[main] Wi-Fi connected! IP: %s, RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());

        // Sync connected SSID and password into AppSettings
        if (WiFi.SSID().length() > 0) {
            strncpy(settings.wifi_ssid, WiFi.SSID().c_str(), sizeof(settings.wifi_ssid) - 1);
            if (WiFi.psk().length() > 0) {
                strncpy(settings.wifi_password, WiFi.psk().c_str(), sizeof(settings.wifi_password) - 1);
            }
            settings_save(&settings);
        }

        // Configure local timezone and start NTP synchronization
        const char *posix_tz = settings_get_tz_posix();
        setenv("TZ", posix_tz, 1);
        tzset();
        configTzTime(posix_tz, "pool.ntp.org", "time.nist.gov");

        // Start Web Configuration Server & mDNS
        web_config_server_begin();

        // Launch background FreeRTOS tasks on Core 0
        tempest_udp_start();
        tempest_rest_start();
    } else {
        Serial.println("[main] Wi-Fi connection timed out. Starting UDP in case network reconnects...");
        tempest_udp_start();
    }

    Serial.println("[main] Setup complete. Entering main render loop.");
}

void loop() {
#if !BOARD_HAS_WIFIMANAGER
    wifi_portal_tick();
#endif

    // Dynamically monitor Wi-Fi status and ensure web server is running
    static bool s_was_connected = false;
    bool is_conn = (WiFi.status() == WL_CONNECTED);

    if (is_conn && !s_was_connected) {
        s_was_connected = true;
        Serial.printf("[main] Wi-Fi link UP! IP: %s\n", WiFi.localIP().toString().c_str());
        web_config_server_begin();
        tempest_udp_start();
        tempest_rest_start();
    } else if (!is_conn && s_was_connected) {
        s_was_connected = false;
        Serial.println("[main] Wi-Fi link DOWN! Reconnecting in background...");
        WiFi.reconnect();
    }

    // Service HTTP requests for the Web Settings Dashboard
    web_config_server_loop();

    // Run LVGL timer handler (UI animations, smooth needle movement, touch indev)
    display::loop();

    uint32_t now = millis();

    // Update screen data every 500ms
    if (now - s_last_ui_update_ms >= 500) {
        s_last_ui_update_ms = now;
        ui_update();
    }

    // Auto-dim screen when inactive (based on user settings)
    AppSettings cur_settings;
    settings_get(&cur_settings);
    uint32_t idle_ms = display::inactiveMs();

    if (cur_settings.dim_timeout_s == 0) {
        if (s_is_dimmed) {
            s_is_dimmed = false;
            display::setBrightness(cur_settings.brightness_day);
        }
    } else {
        uint32_t dim_ms = (uint32_t)cur_settings.dim_timeout_s * 1000UL;
        if (idle_ms >= dim_ms && !s_is_dimmed) {
            s_is_dimmed = true;
            uint8_t target_b = cur_settings.brightness_dim;

            // Check if current time falls into night schedule
            if (cur_settings.night_mode_enabled) {
                time_t now_sec = time(nullptr);
                if (now_sec > 1000000000LL) {
                    struct tm ti;
                    localtime_r(&now_sec, &ti);
                    int h = ti.tm_hour;
                    bool in_night = false;
                    if (cur_settings.night_start_hour > cur_settings.night_end_hour) {
                        in_night = (h >= cur_settings.night_start_hour || h < cur_settings.night_end_hour);
                    } else {
                        in_night = (h >= cur_settings.night_start_hour && h < cur_settings.night_end_hour);
                    }
                    if (in_night) {
                        target_b = cur_settings.brightness_night;
                    }
                }
            }
            display::setBrightness(target_b);
        } else if (idle_ms < dim_ms && s_is_dimmed) {
            s_is_dimmed = false;
            display::setBrightness(cur_settings.brightness_day);
        }
    }

    // Auto-scroll through screens at timed intervals
    static uint32_t s_last_scroll_ms = 0;
    if (cur_settings.auto_scroll_s > 0) {
        uint32_t interval_ms = (uint32_t)cur_settings.auto_scroll_s * 1000UL;
        // Only auto-scroll if the screen hasn't just been touched (pause on touch for at least interval_ms)
        if (idle_ms >= 3000 && now - s_last_scroll_ms >= interval_ms) {
            s_last_scroll_ms = now;
            ui_next_screen();
        }
    } else {
        s_last_scroll_ms = now;
    }

    // Brief yield for RTOS scheduler
    delay(5);
}
