#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "config.h"
#include "settings_mgr.h"
#include "web_config_server.h"
#include "brightness_mgr.h"
#include "display.h"
#include "tempest_state.h"
#include "tempest_udp.h"
#include "tempest_rest.h"
#include "ui/ui.h"

static DNSServer s_dns;
static bool s_ap_up = false;
static bool s_was_connected = false;
static bool s_had_connection = false;
static uint32_t s_disconnected_since = 0;

static void start_setup_ap() {
    if (s_ap_up) return;
    AppSettings s;
    settings_get(&s);
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAP(AP_NAME, s.admin_password)) {
        s_ap_up = true;
        s_dns.start(53, "*", WiFi.softAPIP());
        web_config_server_set_setup_ap(true);
        if (s.wifi_ssid[0]) WiFi.begin(s.wifi_ssid, s.wifi_password);
        Serial.printf("[wifi] Setup hotspot active: %s\n", AP_NAME);
    }
}

void network_credentials_changed() {
    s_had_connection = false;
    s_was_connected = false;
    s_disconnected_since = millis();
}

static void network_tick() {
    bool connected = WiFi.status() == WL_CONNECTED;
    uint32_t now = millis();
    if (connected && !s_was_connected) {
        s_was_connected = true;
        configTzTime(settings_get_tz_posix(), "pool.ntp.org", "time.nist.gov");
        web_config_server_network_changed();
        tempest_udp_start();
        tempest_rest_start();
        s_had_connection = true;
        tempest_rest_trigger_now();
        if (s_ap_up) {
            s_dns.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            s_ap_up = false;
            web_config_server_set_setup_ap(false);
        }
        Serial.printf("[wifi] Connected: %s\n", WiFi.localIP().toString().c_str());
    } else if (!connected) {
        if (s_was_connected) {
            s_was_connected = false;
            s_disconnected_since = now;
            web_config_server_network_changed();
        }
        // Auto-reconnect covers a short dropout. The setup AP waits until that
        // attempt has had time to fail, and longer after a link that was already up.
        uint32_t portal_after = s_had_connection ? 180000UL : 45000UL;
        if (uint32_t(now - s_disconnected_since) >= portal_after) start_setup_ap();
    }
    if (s_ap_up) s_dns.processNextRequest();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n%s v%s - %s\n", FW_NAME, FW_VERSION, BOARD_NAME);
    tempest_state_init();
    settings_init();
    if (!display::begin()) {
        Serial.println("[main] Display initialization failed; restarting");
        delay(3000);
        ESP.restart();
    }
    ui_init();
    ui_update();
    AppSettings s;
    settings_get(&s);
    brightness_tick(s, 0);
    ui_set_screen(s.wifi_ssid[0] ? s.startup_screen : 0);
    display::loop();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_HOSTNAME);
    WiFi.setAutoReconnect(true);
    s_disconnected_since = millis();
    if (s.wifi_ssid[0]) WiFi.begin(s.wifi_ssid, s.wifi_password);
    else start_setup_ap();
    web_config_server_begin();
    tempest_udp_start();
    tempest_rest_start();
}

void loop() {
    network_tick();
    web_config_server_loop();
    display::loop();
    uint32_t now = millis();
    static uint32_t last_stack_log = 0;
    if (uint32_t(now - last_stack_log) >= 60000) {
        last_stack_log = now;
        Serial.printf("[mem] stack free words: loop %u, udp %u, rest %u, heap %u\n",
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                      (unsigned)tempest_udp_stack_free(),
                      (unsigned)tempest_rest_stack_free(),
                      (unsigned)ESP.getFreeHeap());
    }
    static uint32_t last_ui = 0;
    if (uint32_t(now - last_ui) >= 500) {
        last_ui = now;
        tempest_state_tick();
        ui_update();
    }
    AppSettings s;
    settings_get(&s);
    uint32_t idle = display::inactiveMs();
    brightness_tick(s, idle);
    static uint32_t last_scroll = 0;
    if (s.auto_scroll_s) {
        uint32_t interval = uint32_t(s.auto_scroll_s) * 1000UL;
        if (idle >= interval && uint32_t(now - last_scroll) >= interval) {
            last_scroll = now;
            ui_next_screen(s.auto_scroll_mask);
        }
    } else last_scroll = now;
    delay(5);
}
