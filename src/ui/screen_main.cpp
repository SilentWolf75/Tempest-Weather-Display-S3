#include "screen_main.h"
#include "weather_anim.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

LV_FONT_DECLARE(montserrat_64_digits);

#define COL_BG          lv_color_black()
#define COL_TEXT_MAIN   lv_color_hex(0xF8FAFC)
#define COL_TEXT_SOFT   lv_color_hex(0x94A3B8)
#define COL_ACCENT_CYAN lv_color_hex(0x38BDF8)
#define COL_ACCENT_AMBER lv_color_hex(0xFBBF24)
#define COL_PANEL_BG    lv_color_hex(0x0F172A)
#define COL_PANEL_BORDER lv_color_hex(0x1E293B)
#define COL_PULSE_GREEN lv_color_hex(0x22C55E)

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_time_lbl = nullptr;
static lv_obj_t *s_udp_pulse = nullptr;
static lv_obj_t *s_icon_anim = nullptr;
static lv_obj_t *s_temp_lbl = nullptr;
static lv_obj_t *s_unit_lbl = nullptr;
static lv_obj_t *s_cond_lbl = nullptr;
static lv_obj_t *s_range_lbl = nullptr;
static lv_obj_t *s_humid_val_lbl = nullptr;
static lv_obj_t *s_humid_sub_lbl = nullptr;
static lv_obj_t *s_baro_val_lbl = nullptr;
static lv_obj_t *s_baro_sub_lbl = nullptr;
static lv_obj_t *s_footer_lbl = nullptr;

static void format_time(char *buf, size_t len) {
    time_t now = time(nullptr);
    if (now < 1000000000LL) {
        snprintf(buf, len, "--:--");
        return;
    }
    struct tm ti;
    localtime_r(&now, &ti);
    int h = ti.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(buf, len, "%d:%02d %s", h, ti.tm_min, ti.tm_hour < 12 ? "AM" : "PM");
}

lv_obj_t* screen_main_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle ambient decorative circle ring
    lv_obj_t *ring = lv_obj_create(s_panel);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 456, 456);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(ring, COL_ACCENT_CYAN, 0);
    lv_obj_set_style_border_opa(ring, 40, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // --- Header (y = 35) ---
    s_time_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_time_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_time_lbl, "12:00 PM");
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 36);

    // UDP heartbeat pulse dot (left of time)
    s_udp_pulse = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_udp_pulse);
    lv_obj_set_size(s_udp_pulse, 8, 8);
    lv_obj_set_style_radius(s_udp_pulse, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_udp_pulse, COL_PULSE_GREEN, 0);
    lv_obj_set_style_bg_opa(s_udp_pulse, LV_OPA_COVER, 0);
    lv_obj_align(s_udp_pulse, LV_ALIGN_TOP_MID, -75, 42);

    // --- Center Weather Section (y = 80..210) ---
    // Animated weather icon (130x130 padded canvas, no clipping)
    s_icon_anim = weather_anim_create(s_panel, 130, 130);
    lv_obj_align(s_icon_anim, LV_ALIGN_TOP_LEFT, 80, 78);

    // Giant temperature numbers
    s_temp_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_temp_lbl, &montserrat_64_digits, 0);
    lv_obj_set_style_text_color(s_temp_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_temp_lbl, "72");
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_LEFT, 222, 108);

    // Temperature unit (°F / °C)
    s_unit_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_unit_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_unit_lbl, COL_ACCENT_AMBER, 0);
    lv_label_set_text(s_unit_lbl, "°F");
    lv_obj_align_to(s_unit_lbl, s_temp_lbl, LV_ALIGN_OUT_RIGHT_TOP, 4, 10);

    // Condition text (y = 210)
    s_cond_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_cond_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_cond_lbl, COL_ACCENT_CYAN, 0);
    lv_obj_set_style_text_align(s_cond_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_cond_lbl, "Partly Cloudy");
    lv_obj_align(s_cond_lbl, LV_ALIGN_CENTER, 0, -20);

    // High / Low & Feels Like (y = 240)
    s_range_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_range_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_range_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_align(s_range_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_range_lbl, "H: --   L: --  |  Feels --");
    lv_obj_align(s_range_lbl, LV_ALIGN_CENTER, 0, 10);

    // --- Twin Metric Cards (y = 280..350) ---
    // Humidity Card (left)
    lv_obj_t *humid_card = lv_obj_create(s_panel);
    lv_obj_remove_style_all(humid_card);
    lv_obj_set_size(humid_card, 150, 68);
    lv_obj_align(humid_card, LV_ALIGN_CENTER, -82, 68);
    lv_obj_set_style_bg_color(humid_card, COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(humid_card, 220, 0);
    lv_obj_set_style_radius(humid_card, 16, 0);
    lv_obj_set_style_border_color(humid_card, COL_PANEL_BORDER, 0);
    lv_obj_set_style_border_width(humid_card, 1, 0);

    lv_obj_t *h_icon = lv_label_create(humid_card);
    lv_obj_set_style_text_font(h_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(h_icon, COL_ACCENT_CYAN, 0);
    lv_label_set_text(h_icon, "HUMIDITY");
    lv_obj_align(h_icon, LV_ALIGN_TOP_MID, 0, 8);

    s_humid_val_lbl = lv_label_create(humid_card);
    lv_obj_set_style_text_font(s_humid_val_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_humid_val_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_humid_val_lbl, "54%");
    lv_obj_align(s_humid_val_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    // Pressure Card (right)
    lv_obj_t *baro_card = lv_obj_create(s_panel);
    lv_obj_remove_style_all(baro_card);
    lv_obj_set_size(baro_card, 150, 68);
    lv_obj_align(baro_card, LV_ALIGN_CENTER, 82, 68);
    lv_obj_set_style_bg_color(baro_card, COL_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(baro_card, 220, 0);
    lv_obj_set_style_radius(baro_card, 16, 0);
    lv_obj_set_style_border_color(baro_card, COL_PANEL_BORDER, 0);
    lv_obj_set_style_border_width(baro_card, 1, 0);

    lv_obj_t *b_icon = lv_label_create(baro_card);
    lv_obj_set_style_text_font(b_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(b_icon, COL_ACCENT_AMBER, 0);
    lv_label_set_text(b_icon, "PRESSURE");
    lv_obj_align(b_icon, LV_ALIGN_TOP_MID, 0, 8);

    s_baro_val_lbl = lv_label_create(baro_card);
    lv_obj_set_style_text_font(s_baro_val_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_baro_val_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_baro_val_lbl, "29.92");
    lv_obj_align(s_baro_val_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    // --- Footer info (Rain & UV, y = 370) ---
    s_footer_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_footer_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_footer_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_align(s_footer_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_footer_lbl, "UV: 3.4 Moderate  ·  Rain Today: 0.00 in");
    lv_obj_align(s_footer_lbl, LV_ALIGN_CENTER, 0, 132);

    return s_panel;
}

void screen_main_update(const TempestState &state) {
    if (!s_panel) return;

    // Time update
    char tbuf[24];
    format_time(tbuf, sizeof(tbuf));
    lv_label_set_text(s_time_lbl, tbuf);

    // Heartbeat pulse dot: blink briefly if packet was received recently
    uint32_t elapsed_ms = millis() - state.last_packet_millis;
    if (elapsed_ms < 1200) {
        lv_obj_set_style_bg_opa(s_udp_pulse, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(s_udp_pulse, 40, 0);
    }

    // Weather icon
    weather_anim_set_icon(s_icon_anim, state.icon_slug);

    // Temperature & Unit
    float t_display = temp_to_unit(state.air_temp_c, state.units);
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.0f", roundf(t_display));
    lv_label_set_text(s_temp_lbl, temp_str);
    lv_label_set_text(s_unit_lbl, temp_unit_str(state.units));

    // Conditions
    lv_label_set_text(s_cond_lbl, state.conditions_text);

    // High / Low / Feels Like
    float h_disp = temp_to_unit(state.temp_high_c, state.units);
    float l_disp = temp_to_unit(state.temp_low_c, state.units);
    float fl_disp = temp_to_unit(state.feels_like_c, state.units);
    char range_buf[64];
    snprintf(range_buf, sizeof(range_buf), "H: %.0f  L: %.0f  |  Feels %.0f",
             roundf(h_disp), roundf(l_disp), roundf(fl_disp));
    lv_label_set_text(s_range_lbl, range_buf);

    // Humidity
    char hum_buf[16];
    snprintf(hum_buf, sizeof(hum_buf), "%.0f%%", state.humidity_pct);
    lv_label_set_text(s_humid_val_lbl, hum_buf);

    // Pressure & Tendency
    float p_disp = pressure_to_unit(state.pressure_mb, state.units);
    const char *trend_arrow = ">";
    if (state.pressure_trend_mb > 0.8f) trend_arrow = "^";
    else if (state.pressure_trend_mb < -0.8f) trend_arrow = "v";

    char baro_buf[24];
    if (state.units == UNIT_IMPERIAL) {
        snprintf(baro_buf, sizeof(baro_buf), "%.2f %s", p_disp, trend_arrow);
    } else {
        snprintf(baro_buf, sizeof(baro_buf), "%.0f %s", roundf(p_disp), trend_arrow);
    }
    lv_label_set_text(s_baro_val_lbl, baro_buf);

    // Footer: Alternate every 4 seconds between weather stats and Web Setup URL
    char foot_buf[64];
    if (WiFi.status() == WL_CONNECTED) {
        bool show_url = ((millis() / 4000) % 2 == 1);
        if (show_url) {
            snprintf(foot_buf, sizeof(foot_buf), "Web: http://%s/", WiFi.localIP().toString().c_str());
        } else {
            const char *uv_desc = "Low";
            if (state.uv_index >= 3.0f && state.uv_index < 6.0f) uv_desc = "Mod";
            else if (state.uv_index >= 6.0f && state.uv_index < 8.0f) uv_desc = "High";
            else if (state.uv_index >= 8.0f) uv_desc = "Very High";

            float rain_disp = (state.units == UNIT_IMPERIAL) ? (state.rain_today_mm * 0.0393701f) : state.rain_today_mm;
            const char *rain_unit = (state.units == UNIT_IMPERIAL) ? "in" : "mm";
            snprintf(foot_buf, sizeof(foot_buf), "UV: %.1f %s  |  Rain: %.2f %s",
                     state.uv_index, uv_desc, rain_disp, rain_unit);
        }
    } else {
        snprintf(foot_buf, sizeof(foot_buf), "Wi-Fi: Connecting / Setup AP...");
    }
    lv_label_set_text(s_footer_lbl, foot_buf);
}
