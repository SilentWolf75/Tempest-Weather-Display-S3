#include "screen_info.h"
#include "config.h"
#include "settings_mgr.h"
#include "tempest_state.h"
#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>

#define COL_BG          lv_color_black()
#define COL_CARD_BG     lv_color_hex(0x0F172A)
#define COL_CARD_BORDER lv_color_hex(0x1E293B)
#define COL_TEXT_MAIN   lv_color_hex(0xF8FAFC)
#define COL_TEXT_SOFT   lv_color_hex(0x94A3B8)
#define COL_ACCENT_CYAN lv_color_hex(0x38BDF8)
#define COL_ACCENT_AMBER lv_color_hex(0xFBBF24)
#define COL_ONLINE_GREEN lv_color_hex(0x22C55E)
#define COL_WARN_RED    lv_color_hex(0xEF4444)

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_status_dot = nullptr;
static lv_obj_t *s_status_badge = nullptr;
static lv_obj_t *s_wifi_ssid_lbl = nullptr;
static lv_obj_t *s_wifi_ip_lbl = nullptr;
static lv_obj_t *s_mdns_lbl = nullptr;
static lv_obj_t *s_station_lbl = nullptr;
static lv_obj_t *s_feed_lbl = nullptr;
static lv_obj_t *s_mac_lbl = nullptr;
static lv_obj_t *s_uptime_lbl = nullptr;

lv_obj_t* screen_info_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Decorative circular accent ring (matches Screen 1/2/3 aesthetic)
    lv_obj_t *ring = lv_obj_create(s_panel);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 456, 456);
    lv_obj_center(ring);
    lv_obj_set_style_border_color(ring, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_50, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Status Pill Container (y = 42)
    lv_obj_t *status_cont = lv_obj_create(s_panel);
    lv_obj_remove_style_all(status_cont);
    lv_obj_set_size(status_cont, 260, 20);
    lv_obj_align(status_cont, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_clear_flag(status_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Glowing Dot
    s_status_dot = lv_obj_create(status_cont);
    lv_obj_remove_style_all(s_status_dot);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_status_dot, COL_ONLINE_GREEN, 0);
    lv_obj_set_style_bg_opa(s_status_dot, LV_OPA_COVER, 0);
    lv_obj_align(s_status_dot, LV_ALIGN_LEFT_MID, 22, 0);

    // Status Text
    s_status_badge = lv_label_create(status_cont);
    lv_label_set_text(s_status_badge, "ONLINE (Wi-Fi Connected)");
    lv_obj_set_style_text_color(s_status_badge, COL_ONLINE_GREEN, 0);
    lv_obj_set_style_text_font(s_status_badge, &lv_font_montserrat_12, 0);
    lv_obj_align(s_status_badge, LV_ALIGN_LEFT_MID, 36, 0);

    // 2. Network Card (y = 74..198, width 340, height 124)
    lv_obj_t *card_net = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_net);
    lv_obj_set_size(card_net, 340, 124);
    lv_obj_align(card_net, LV_ALIGN_TOP_MID, 0, 74);
    lv_obj_set_style_bg_color(card_net, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_net, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_net, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_net, 1, 0);
    lv_obj_set_style_radius(card_net, 16, 0);
    lv_obj_set_style_pad_hor(card_net, 16, 0);
    lv_obj_set_style_pad_ver(card_net, 10, 0);
    lv_obj_clear_flag(card_net, LV_OBJ_FLAG_SCROLLABLE);

    // Network Card Title
    lv_obj_t *net_hdr = lv_label_create(card_net);
    lv_label_set_text(net_hdr, LV_SYMBOL_WIFI "  NETWORK");
    lv_obj_set_style_text_color(net_hdr, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_font(net_hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(net_hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    // Wi-Fi SSID
    s_wifi_ssid_lbl = lv_label_create(card_net);
    lv_label_set_text(s_wifi_ssid_lbl, "Wi-Fi: --");
    lv_obj_set_style_text_color(s_wifi_ssid_lbl, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(s_wifi_ssid_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_wifi_ssid_lbl, LV_ALIGN_TOP_LEFT, 0, 18);

    // IP Address
    s_wifi_ip_lbl = lv_label_create(card_net);
    lv_label_set_text(s_wifi_ip_lbl, "IP: 0.0.0.0");
    lv_obj_set_style_text_color(s_wifi_ip_lbl, COL_ACCENT_CYAN, 0);
    lv_obj_set_style_text_font(s_wifi_ip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_ip_lbl, LV_ALIGN_TOP_LEFT, 0, 38);

    // Web Config URL
    s_mdns_lbl = lv_label_create(card_net);
    lv_label_set_text(s_mdns_lbl, "Web: http://weather.local/");
    lv_obj_set_style_text_color(s_mdns_lbl, COL_ACCENT_AMBER, 0);
    lv_obj_set_style_text_font(s_mdns_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_mdns_lbl, LV_ALIGN_TOP_LEFT, 0, 64);

    // MAC Address
    s_mac_lbl = lv_label_create(card_net);
    lv_label_set_text(s_mac_lbl, "MAC: --:--:--:--:--:--");
    lv_obj_set_style_text_color(s_mac_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_font(s_mac_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_mac_lbl, LV_ALIGN_TOP_LEFT, 0, 84);

    // 3. Tempest Station Card (y = 206..296, width 340, height 90)
    lv_obj_t *card_st = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_st);
    lv_obj_set_size(card_st, 340, 90);
    lv_obj_align(card_st, LV_ALIGN_TOP_MID, 0, 206);
    lv_obj_set_style_bg_color(card_st, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_st, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_st, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_st, 1, 0);
    lv_obj_set_style_radius(card_st, 16, 0);
    lv_obj_set_style_pad_hor(card_st, 16, 0);
    lv_obj_set_style_pad_ver(card_st, 10, 0);
    lv_obj_clear_flag(card_st, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *st_hdr = lv_label_create(card_st);
    lv_label_set_text(st_hdr, LV_SYMBOL_PLAY "  TEMPEST STATION");
    lv_obj_set_style_text_color(st_hdr, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_font(st_hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(st_hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    s_station_lbl = lv_label_create(card_st);
    lv_label_set_text(s_station_lbl, "Station: Not Configured");
    lv_obj_set_style_text_color(s_station_lbl, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(s_station_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_station_lbl, LV_ALIGN_TOP_LEFT, 0, 20);

    s_feed_lbl = lv_label_create(card_st);
    lv_label_set_text(s_feed_lbl, "Feed: Waiting for packets...");
    lv_obj_set_style_text_color(s_feed_lbl, COL_ACCENT_CYAN, 0);
    lv_obj_set_style_text_font(s_feed_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_feed_lbl, LV_ALIGN_TOP_LEFT, 0, 46);

    // 4. Lower Hardware & System Card (y = 304..374, width 280, height 70)
    lv_obj_t *card_sys = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_sys);
    lv_obj_set_size(card_sys, 280, 70);
    lv_obj_align(card_sys, LV_ALIGN_TOP_MID, 0, 304);
    lv_obj_set_style_bg_color(card_sys, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_sys, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card_sys, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_sys, 1, 0);
    lv_obj_set_style_radius(card_sys, 14, 0);
    lv_obj_set_style_pad_hor(card_sys, 12, 0);
    lv_obj_set_style_pad_ver(card_sys, 8, 0);
    lv_obj_clear_flag(card_sys, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fw_lbl = lv_label_create(card_sys);
    lv_label_set_text(fw_lbl, "Firmware: v" FW_VERSION " (ESP32-S3)");
    lv_obj_set_style_text_color(fw_lbl, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(fw_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(fw_lbl, LV_ALIGN_TOP_MID, 0, 4);

    s_uptime_lbl = lv_label_create(card_sys);
    lv_label_set_text(s_uptime_lbl, "Uptime: 00h 00m 00s");
    lv_obj_set_style_text_color(s_uptime_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_font(s_uptime_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(s_uptime_lbl, LV_ALIGN_TOP_MID, 0, 28);

    return s_panel;
}

void screen_info_update(const TempestState &state) {
    if (!s_panel) return;

    AppSettings settings;
    settings_get(&settings);

    // 1. Wi-Fi & Status Badge
    char buf[64];
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(s_status_badge, "ONLINE (Wi-Fi Connected)");
        lv_obj_set_style_text_color(s_status_badge, COL_ONLINE_GREEN, 0);
        if (s_status_dot) lv_obj_set_style_bg_color(s_status_dot, COL_ONLINE_GREEN, 0);

        int rssi = WiFi.RSSI();
        const char *quality = (rssi > -60) ? "Excellent" : (rssi > -70) ? "Good" : "Fair";
        snprintf(buf, sizeof(buf), "Wi-Fi: %s (%d dBm, %s)", WiFi.SSID().c_str(), rssi, quality);
        lv_label_set_text(s_wifi_ssid_lbl, buf);

        snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
        lv_label_set_text(s_wifi_ip_lbl, buf);

        lv_label_set_text(s_mdns_lbl, "Web: http://weather.local/");
    } else if (WiFi.getMode() & WIFI_MODE_AP) {
        lv_label_set_text(s_status_badge, "SETUP MODE (Hotspot Active)");
        lv_obj_set_style_text_color(s_status_badge, COL_ACCENT_AMBER, 0);
        if (s_status_dot) lv_obj_set_style_bg_color(s_status_dot, COL_ACCENT_AMBER, 0);

        snprintf(buf, sizeof(buf), "Hotspot: %s", AP_NAME);
        lv_label_set_text(s_wifi_ssid_lbl, buf);
        lv_label_set_text(s_wifi_ip_lbl, "IP: 192.168.4.1");
        lv_label_set_text(s_mdns_lbl, "Web: http://192.168.4.1/");
    } else {
        lv_label_set_text(s_status_badge, "OFFLINE (Reconnecting)");
        lv_obj_set_style_text_color(s_status_badge, COL_WARN_RED, 0);
        if (s_status_dot) lv_obj_set_style_bg_color(s_status_dot, COL_WARN_RED, 0);

        if (strlen(settings.wifi_ssid) > 0) {
            snprintf(buf, sizeof(buf), "Wi-Fi: %s (Connecting...)", settings.wifi_ssid);
        } else {
            snprintf(buf, sizeof(buf), "Wi-Fi: Not Configured");
        }
        lv_label_set_text(s_wifi_ssid_lbl, buf);
        lv_label_set_text(s_wifi_ip_lbl, "IP: Disconnected");
        lv_label_set_text(s_mdns_lbl, "Web: Unavailable");
    }

    // MAC
    snprintf(buf, sizeof(buf), "MAC: %s", WiFi.macAddress().c_str());
    lv_label_set_text(s_mac_lbl, buf);

    // 2. Tempest Station Info
    if (settings.station_id > 0) {
        snprintf(buf, sizeof(buf), "Station: #%u", settings.station_id);
    } else {
        snprintf(buf, sizeof(buf), "Station: None (Set in Web Config)");
    }
    lv_label_set_text(s_station_lbl, buf);

    if (state.last_obs_epoch > 0) {
        time_t age = time(nullptr) - state.last_obs_epoch;
        if (age < 90) {
            snprintf(buf, sizeof(buf), "Feed: Live UDP Broadcast (%llds ago)", (long long)age);
            lv_obj_set_style_text_color(s_feed_lbl, COL_ONLINE_GREEN, 0);
        } else {
            snprintf(buf, sizeof(buf), "Feed: Cloud REST Backup (%llds ago)", (long long)age);
            lv_obj_set_style_text_color(s_feed_lbl, COL_ACCENT_AMBER, 0);
        }
    } else {
        snprintf(buf, sizeof(buf), "Feed: Listening on UDP port %d...", TEMPEST_UDP_PORT);
        lv_obj_set_style_text_color(s_feed_lbl, COL_TEXT_SOFT, 0);
    }
    lv_label_set_text(s_feed_lbl, buf);

    // 3. System Uptime
    uint32_t s = millis() / 1000;
    uint32_t d = s / 86400;
    uint32_t h = (s % 86400) / 3600;
    uint32_t m = (s % 3600) / 60;
    uint32_t sec = s % 60;
    if (d > 0) {
        snprintf(buf, sizeof(buf), "Uptime: %ud %02uh %02um %02us", d, h, m, sec);
    } else {
        snprintf(buf, sizeof(buf), "Uptime: %02uh %02um %02us", h, m, sec);
    }
    lv_label_set_text(s_uptime_lbl, buf);
}
