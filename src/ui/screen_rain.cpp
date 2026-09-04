#include "screen_rain.h"
#include "config.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#define COL_BG          lv_color_black()
#define COL_CARD_BG     lv_color_hex(0x0A1322)
#define COL_CARD_BORDER lv_color_hex(0x1E293B)
#define COL_TEXT_MAIN   lv_color_hex(0xF8FAFC)
#define COL_TEXT_SOFT   lv_color_hex(0x94A3B8)
#define COL_TEXT_MUTED  lv_color_hex(0x64748B)

#define COL_RAIN_CYAN   lv_color_hex(0x38BDF8)
#define COL_RAIN_LIGHT  lv_color_hex(0x0EA5E9)
#define COL_RAIN_MOD    lv_color_hex(0x10B981)
#define COL_RAIN_HEAVY  lv_color_hex(0xF59E0B)
#define COL_RAIN_VIOLENT lv_color_hex(0xEF4444)

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_udp_pulse = nullptr;
static lv_obj_t *s_time_lbl = nullptr;
static lv_obj_t *s_badge = nullptr;
static lv_obj_t *s_badge_lbl = nullptr;

static lv_obj_t *s_rate_arc = nullptr;
static lv_obj_t *s_drop_icon = nullptr;
static lv_obj_t *s_accum_val_lbl = nullptr;
static lv_obj_t *s_accum_unit_lbl = nullptr;
static lv_obj_t *s_accum_sub_lbl = nullptr;

static lv_obj_t *s_rate_val_lbl = nullptr;
static lv_obj_t *s_rate_cat_lbl = nullptr;
static lv_obj_t *s_chance_val_lbl = nullptr;
static lv_obj_t *s_yesterday_val_lbl = nullptr;
static lv_obj_t *s_footer_lbl = nullptr;

static void format_time(char *buf, size_t len) {
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    int h = ti.tm_hour;
    int m = ti.tm_min;
    const char *ampm = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, len, "%d:%02d %s", h12, m, ampm);
}

lv_obj_t* screen_rain_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Heartbeat pulse dot (Green)
    s_udp_pulse = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_udp_pulse);
    lv_obj_set_size(s_udp_pulse, 7, 7);
    lv_obj_set_style_radius(s_udp_pulse, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_udp_pulse, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_bg_opa(s_udp_pulse, LV_OPA_COVER, 0);
    lv_obj_align(s_udp_pulse, LV_ALIGN_TOP_MID, -75, 42);

    // Local Time
    s_time_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_time_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_time_lbl, "12:00 PM");
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 36);

    // Status Pill Badge (y = 66)
    s_badge = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_badge);
    lv_obj_set_size(s_badge, 190, 24);
    lv_obj_align(s_badge, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_badge, 12, 0);
    lv_obj_set_style_border_color(s_badge, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(s_badge, 1, 0);

    s_badge_lbl = lv_label_create(s_badge);
    lv_obj_set_style_text_font(s_badge_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_badge_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_badge_lbl, "NO RAIN DETECTED");
    lv_obj_align(s_badge_lbl, LV_ALIGN_CENTER, 0, 0);

    // Rain Rate Arc Gauge (220x220 centered at y = -32)
    s_rate_arc = lv_arc_create(s_panel);
    lv_obj_remove_style_all(s_rate_arc);
    lv_obj_set_size(s_rate_arc, 220, 220);
    lv_obj_align(s_rate_arc, LV_ALIGN_CENTER, 0, -32);
    lv_arc_set_rotation(s_rate_arc, 135);
    lv_arc_set_bg_angles(s_rate_arc, 0, 270);
    lv_arc_set_angles(s_rate_arc, 0, 0);
    lv_arc_set_range(s_rate_arc, 0, 100);
    lv_obj_set_style_arc_width(s_rate_arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_rate_arc, COL_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_rate_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_rate_arc, COL_RAIN_CYAN, LV_PART_INDICATOR);
    lv_obj_clear_flag(s_rate_arc, LV_OBJ_FLAG_CLICKABLE);

    // Droplet Icon
    s_drop_icon = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_drop_icon, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_drop_icon, COL_RAIN_CYAN, 0);
    lv_label_set_text(s_drop_icon, LV_SYMBOL_TINT);
    lv_obj_align(s_drop_icon, LV_ALIGN_CENTER, 0, -68);

    // Hero Accumulation Digits
    s_accum_val_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_accum_val_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_accum_val_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_accum_val_lbl, "0.00");
    lv_obj_align(s_accum_val_lbl, LV_ALIGN_CENTER, -16, -26);

    // Hero Unit ("in" / "mm")
    s_accum_unit_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_accum_unit_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_accum_unit_lbl, COL_RAIN_CYAN, 0);
    lv_label_set_text(s_accum_unit_lbl, "in");
    lv_obj_align_to(s_accum_unit_lbl, s_accum_val_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);

    // Subtitle ("TODAY'S RAINFALL")
    s_accum_sub_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_accum_sub_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_accum_sub_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_accum_sub_lbl, "TODAY'S RAINFALL");
    lv_obj_align(s_accum_sub_lbl, LV_ALIGN_CENTER, 0, 10);

    // --- Twin Metric Cards (y = 265..330) ---
    // Left Card: Rain Rate & Category
    lv_obj_t *card_rate = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_rate);
    lv_obj_set_size(card_rate, 148, 64);
    lv_obj_align(card_rate, LV_ALIGN_CENTER, -82, 60);
    lv_obj_set_style_bg_color(card_rate, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_rate, 220, 0);
    lv_obj_set_style_radius(card_rate, 16, 0);
    lv_obj_set_style_border_color(card_rate, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_rate, 1, 0);

    lv_obj_t *hdr_rate = lv_label_create(card_rate);
    lv_obj_set_style_text_font(hdr_rate, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_rate, COL_RAIN_CYAN, 0);
    lv_label_set_text(hdr_rate, "RAIN RATE");
    lv_obj_align(hdr_rate, LV_ALIGN_TOP_MID, 0, 5);

    s_rate_val_lbl = lv_label_create(card_rate);
    lv_obj_set_style_text_font(s_rate_val_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_rate_val_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_rate_val_lbl, "0.00 in/h");
    lv_obj_align(s_rate_val_lbl, LV_ALIGN_TOP_MID, 0, 20);

    s_rate_cat_lbl = lv_label_create(card_rate);
    lv_obj_set_style_text_font(s_rate_cat_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_rate_cat_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_rate_cat_lbl, "Dry");
    lv_obj_align(s_rate_cat_lbl, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Right Card: Rain Probability & Yesterday
    lv_obj_t *card_pop = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_pop);
    lv_obj_set_size(card_pop, 148, 64);
    lv_obj_align(card_pop, LV_ALIGN_CENTER, 82, 60);
    lv_obj_set_style_bg_color(card_pop, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_pop, 220, 0);
    lv_obj_set_style_radius(card_pop, 16, 0);
    lv_obj_set_style_border_color(card_pop, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_pop, 1, 0);

    lv_obj_t *hdr_pop = lv_label_create(card_pop);
    lv_obj_set_style_text_font(hdr_pop, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_pop, COL_RAIN_CYAN, 0);
    lv_label_set_text(hdr_pop, "CHANCE & YEST");
    lv_obj_align(hdr_pop, LV_ALIGN_TOP_MID, 0, 5);

    s_chance_val_lbl = lv_label_create(card_pop);
    lv_obj_set_style_text_font(s_chance_val_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_chance_val_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_chance_val_lbl, "-- POP");
    lv_obj_align(s_chance_val_lbl, LV_ALIGN_TOP_MID, 0, 20);

    s_yesterday_val_lbl = lv_label_create(card_pop);
    lv_obj_set_style_text_font(s_yesterday_val_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_yesterday_val_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_yesterday_val_lbl, "Yest: 0.00 in");
    lv_obj_align(s_yesterday_val_lbl, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Footer info
    s_footer_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_footer_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_footer_lbl, COL_TEXT_MUTED, 0);
    lv_label_set_text(s_footer_lbl, "Conditions dry  •  Tempest haptic sensor active");
    lv_obj_align(s_footer_lbl, LV_ALIGN_CENTER, 0, 108);

    return s_panel;
}

void screen_rain_update(const TempestState &state) {
    if (!s_panel) return;

    // Time
    char tbuf[24];
    format_time(tbuf, sizeof(tbuf));
    lv_label_set_text(s_time_lbl, tbuf);

    // Pulse dot
    uint32_t elapsed_ms = millis() - state.last_packet_millis;
    lv_obj_set_style_bg_opa(s_udp_pulse, (elapsed_ms < 1200) ? LV_OPA_COVER : 40, 0);

    // Unit conversions
    float today_disp = rain_to_unit(state.rain_today_mm, state.units);
    float rate_disp  = rain_to_unit(state.rain_rate_mm_hr, state.units);
    float yest_disp  = rain_to_unit(state.rain_yesterday_mm, state.units);
    const char *u_str = rain_unit_str(state.units);
    const char *r_str = (state.units == UNIT_IMPERIAL) ? "in/h" : "mm/h";

    // Hero Today's accumulation
    char accum_buf[16];
    snprintf(accum_buf, sizeof(accum_buf), "%.2f", today_disp);
    lv_label_set_text(s_accum_val_lbl, accum_buf);
    lv_label_set_text(s_accum_unit_lbl, u_str);
    lv_obj_align_to(s_accum_unit_lbl, s_accum_val_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -8);

    // Intensity calculation & Arc value
    // Arc scale: 0..100 (where 100 is 1.0 in/hr or 25 mm/hr)
    float max_rate = (state.units == UNIT_IMPERIAL) ? 1.0f : 25.0f;
    int arc_val = (int)((rate_disp / max_rate) * 100.0f);
    if (arc_val < 0) arc_val = 0;
    if (arc_val > 100) arc_val = 100;
    lv_arc_set_value(s_rate_arc, arc_val);

    const char *cat_str = "Dry";
    lv_color_t arc_color = COL_RAIN_CYAN;
    bool active = state.is_raining || (state.rain_rate_mm_hr > 0.0f);

    float r_mm = state.rain_rate_mm_hr;
    if (r_mm <= 0.05f) {
        cat_str = "Dry";
        arc_color = COL_RAIN_CYAN;
    } else if (r_mm < 2.5f) {
        cat_str = "Light Rain";
        arc_color = COL_RAIN_LIGHT;
    } else if (r_mm < 7.6f) {
        cat_str = "Moderate Rain";
        arc_color = COL_RAIN_MOD;
    } else if (r_mm < 50.0f) {
        cat_str = "Heavy Rain";
        arc_color = COL_RAIN_HEAVY;
    } else {
        cat_str = "Violent Storm";
        arc_color = COL_RAIN_VIOLENT;
    }

    if (state.precip_type == 2) cat_str = "Hail";
    else if (state.precip_type == 3) cat_str = "Rain + Hail";

    lv_obj_set_style_arc_color(s_rate_arc, arc_color, LV_PART_INDICATOR);

    // Status Badge
    if (active) {
        lv_label_set_text(s_badge_lbl, LV_SYMBOL_TINT "  ACTIVE PRECIPITATION");
        lv_obj_set_style_text_color(s_badge_lbl, COL_RAIN_CYAN, 0);
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x082F49), 0);
        lv_obj_set_style_border_color(s_badge, COL_RAIN_CYAN, 0);
    } else if (today_disp > 0.0f) {
        lv_label_set_text(s_badge_lbl, "PRECIPITATION TODAY");
        lv_obj_set_style_text_color(s_badge_lbl, COL_RAIN_LIGHT, 0);
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x0C1C30), 0);
        lv_obj_set_style_border_color(s_badge, COL_CARD_BORDER, 0);
    } else {
        lv_label_set_text(s_badge_lbl, "NO RAIN DETECTED");
        lv_obj_set_style_text_color(s_badge_lbl, COL_TEXT_SOFT, 0);
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x0F172A), 0);
        lv_obj_set_style_border_color(s_badge, COL_CARD_BORDER, 0);
    }

    // Rate Card
    char rate_buf[24];
    snprintf(rate_buf, sizeof(rate_buf), "%.2f %s", rate_disp, r_str);
    lv_label_set_text(s_rate_val_lbl, rate_buf);
    lv_label_set_text(s_rate_cat_lbl, cat_str);

    // Chance & Yesterday Card
    char chance_buf[24];
    if (state.rain_probability_pct >= 0) {
        snprintf(chance_buf, sizeof(chance_buf), "%d%% POP", state.rain_probability_pct);
    } else {
        snprintf(chance_buf, sizeof(chance_buf), "-- POP");
    }
    lv_label_set_text(s_chance_val_lbl, chance_buf);

    char yest_buf[24];
    snprintf(yest_buf, sizeof(yest_buf), "Yest: %.2f %s", yest_disp, u_str);
    lv_label_set_text(s_yesterday_val_lbl, yest_buf);

    // Footer
    char foot_buf[64];
    if (active) {
        snprintf(foot_buf, sizeof(foot_buf), "Live precip: %s (%.2f %s)", cat_str, rate_disp, r_str);
    } else if (today_disp > 0.0f) {
        snprintf(foot_buf, sizeof(foot_buf), "Rain today: %.2f %s  |  Status: Cleared", today_disp, u_str);
    } else {
        snprintf(foot_buf, sizeof(foot_buf), "Conditions dry  |  Tempest haptic sensor active");
    }
    lv_label_set_text(s_footer_lbl, foot_buf);
}
