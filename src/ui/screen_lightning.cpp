#include "screen_lightning.h"
#include "config.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define COL_BG          lv_color_black()
#define COL_RADAR_RING  lv_color_hex(0x1E293B)
#define COL_ZONE_RED    lv_color_hex(0xEF4444)
#define COL_ZONE_AMBER  lv_color_hex(0xF59E0B)
#define COL_ZONE_YELLOW lv_color_hex(0xEAB308)
#define COL_ZONE_GREEN  lv_color_hex(0x10B981)
#define COL_SWEEP       lv_color_hex(0x38BDF8)
#define COL_TEXT_MAIN   lv_color_hex(0xF8FAFC)
#define COL_TEXT_SOFT   lv_color_hex(0x94A3B8)
#define COL_CARD_BG     lv_color_hex(0x0F172A)
#define COL_CARD_BORDER lv_color_hex(0x1E293B)

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_radar_canvas = nullptr;

// Range Ring distance labels
static lv_obj_t *s_lbl_r1 = nullptr;
static lv_obj_t *s_lbl_r2 = nullptr;
static lv_obj_t *s_lbl_r3 = nullptr;
static lv_obj_t *s_lbl_r4 = nullptr;

// Twin Telemetry Cards
static lv_obj_t *s_cnt3h_lbl = nullptr;
static lv_obj_t *s_cat3h_lbl = nullptr;
static lv_obj_t *s_cnttoday_lbl = nullptr;
static lv_obj_t *s_cattoday_lbl = nullptr;

// Animation state
static float s_ripple_radius = 0.0f;
static bool  s_ripple_active = false;
static float s_last_dist_km = -1.0f;

static void fill_circle(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t r,
                        lv_color_t c, lv_opa_t opa) {
    lv_draw_rect_dsc_t s;
    lv_draw_rect_dsc_init(&s);
    s.bg_color = c;
    s.bg_opa = opa;
    s.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { (lv_coord_t)(cx - r), (lv_coord_t)(cy - r),
                    (lv_coord_t)(cx + r), (lv_coord_t)(cy + r) };
    lv_draw_rect(d, &s, &a);
}

static void draw_ring(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t r,
                      lv_color_t c, lv_coord_t w, lv_opa_t opa) {
    lv_draw_arc_dsc_t s;
    lv_draw_arc_dsc_init(&s);
    s.color = c;
    s.width = w;
    s.opa = opa;
    lv_point_t center = { cx, cy };
    lv_draw_arc(d, &s, &center, r, 0, 360);
}

static void draw_line(lv_draw_ctx_t *d, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                      lv_color_t c, lv_coord_t w, lv_opa_t opa) {
    lv_draw_line_dsc_t s;
    lv_draw_line_dsc_init(&s);
    s.color = c;
    s.width = w;
    s.opa = opa;
    lv_point_t p1 = { x1, y1 }, p2 = { x2, y2 };
    lv_draw_line(d, &s, &p1, &p2);
}

static void radar_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *d = lv_event_get_draw_ctx(e);

    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_coord_t cx = (lv_coord_t)(box.x1 + lv_area_get_width(&box) / 2);
    lv_coord_t cy = (lv_coord_t)(box.y1 + lv_area_get_height(&box) / 2);

    // Outer subtle dial ring
    draw_ring(d, cx, cy, UI_S(215), COL_RADAR_RING, 1, LV_OPA_40);

    // 4 Calibrated Tactical Range Rings: 50, 100, 150, 200 scaled
    // Ring 4 (Outer: 40 mi / 64 km)
    draw_ring(d, cx, cy, UI_S(200), COL_ZONE_GREEN, 1, LV_OPA_30);
    // Ring 3 (20 mi / 32 km)
    draw_ring(d, cx, cy, UI_S(150), COL_ZONE_YELLOW, 1, LV_OPA_40);
    // Ring 2 (10 mi / 16 km)
    draw_ring(d, cx, cy, UI_S(100), COL_ZONE_AMBER,  1, LV_OPA_50);
    // Ring 1 (Inner: 5 mi / 8 km)
    draw_ring(d, cx, cy, UI_S(50),  COL_ZONE_RED,    1, LV_OPA_60);

    // Full Cardinal Crosshairs
    draw_line(d, cx, cy - UI_S(200), cx, cy + UI_S(200), COL_RADAR_RING, 1, LV_OPA_40);
    draw_line(d, cx - UI_S(200), cy, cx + UI_S(200), cy, COL_RADAR_RING, 1, LV_OPA_40);

    // Minor angle tick marks along 200 radius ring (every 30 deg)
    for (int deg = 0; deg < 360; deg += 30) {
        if (deg % 90 == 0) continue; // skip cardinal axes
        float rad = (float)deg * (float)M_PI / 180.0f;
        float s_a = sinf(rad), c_a = cosf(rad);
        draw_line(d,
                  (lv_coord_t)(cx + s_a * UI_S(192)), (lv_coord_t)(cy - c_a * UI_S(192)),
                  (lv_coord_t)(cx + s_a * UI_S(200)), (lv_coord_t)(cy - c_a * UI_S(200)),
                  COL_RADAR_RING, 1, LV_OPA_50);
    }

    // Expanding shockwave ripple if active strike trigger
    if (s_ripple_active && s_ripple_radius > 0.0f) {
        draw_ring(d, cx, cy, (lv_coord_t)s_ripple_radius, COL_ZONE_RED, UI_S(3),
                  (lv_opa_t)(255 * (1.0f - s_ripple_radius / (float)UI_S(205))));
    }

    // Render multi-strike history markers (up to 4 past strikes)
    const float strike_angles[4] = { 45.0f, 135.0f, 225.0f, 315.0f };
    const lv_opa_t strike_opas[4] = { LV_OPA_COVER, LV_OPA_70, LV_OPA_50, LV_OPA_30 };

    TempestState cur_st;
    tempest_get_state(&cur_st);

    for (int i = 0; i < cur_st.recent_strike_count && i < 4; ++i) {
        float d_km = cur_st.recent_strikes[i].dist_km;
        if (d_km <= 0.0f || d_km > 65.0f) continue;

        lv_coord_t max_r = UI_S(200);
        lv_coord_t strike_r = (lv_coord_t)((d_km / 64.0f) * (float)max_r);
        if (strike_r < UI_S(20)) strike_r = UI_S(20);
        if (strike_r > max_r) strike_r = max_r;

        lv_color_t strike_col = (d_km < 8.0f) ? COL_ZONE_RED :
                                ((d_km < 16.0f) ? COL_ZONE_AMBER :
                                ((d_km < 32.0f) ? COL_ZONE_YELLOW : COL_ZONE_GREEN));

        if (i == 0) {
            // Pulsing strike range circle for the newest strike
            draw_ring(d, cx, cy, strike_r, strike_col, UI_S(2), LV_OPA_70);
        }

        // Blip marker at strike distance with spread angles
        float blip_rad = strike_angles[i] * (float)M_PI / 180.0f;
        lv_coord_t bx = (lv_coord_t)(cx + sinf(blip_rad) * strike_r);
        lv_coord_t by = (lv_coord_t)(cy - cosf(blip_rad) * strike_r);

        fill_circle(d, bx, by, UI_S(i == 0 ? 5 : 4), strike_col, strike_opas[i]);
        if (i == 0) {
            draw_ring(d, bx, by, UI_S(9), strike_col, UI_S(2), LV_OPA_70);
        }
    }

    // Center station dot
    fill_circle(d, cx, cy, UI_S(4), COL_SWEEP, LV_OPA_COVER);
}

static void lightning_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_ripple_active) {
        s_ripple_radius += (float)UI_S(5);
        if (s_ripple_radius > (float)UI_S(205)) {
            s_ripple_active = false;
            s_ripple_radius = 0.0f;
        }
        if (s_radar_canvas) lv_obj_invalidate(s_radar_canvas);
    }
}

lv_obj_t* screen_lightning_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Tactical Radar Canvas
    s_radar_canvas = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_radar_canvas);
    lv_obj_set_size(s_radar_canvas, SCREEN_W, SCREEN_H);
    lv_obj_center(s_radar_canvas);
    lv_obj_add_event_cb(s_radar_canvas, radar_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_clear_flag(s_radar_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // --- Range Ring Distance Labels (positioned along 45-deg diagonal) ---
    s_lbl_r1 = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_r1, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_r1, COL_ZONE_RED, 0);
    lv_label_set_text(s_lbl_r1, "5 mi");
    lv_obj_align(s_lbl_r1, LV_ALIGN_CENTER, UI_S(36), UI_S(-36));

    s_lbl_r2 = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_r2, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_r2, COL_ZONE_AMBER, 0);
    lv_label_set_text(s_lbl_r2, "10 mi");
    lv_obj_align(s_lbl_r2, LV_ALIGN_CENTER, UI_S(72), UI_S(-72));

    s_lbl_r3 = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_r3, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_r3, COL_ZONE_YELLOW, 0);
    lv_label_set_text(s_lbl_r3, "20 mi");
    lv_obj_align(s_lbl_r3, LV_ALIGN_CENTER, UI_S(107), UI_S(-107));

    s_lbl_r4 = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl_r4, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_r4, COL_ZONE_GREEN, 0);
    lv_label_set_text(s_lbl_r4, "40 mi");
    lv_obj_align(s_lbl_r4, LV_ALIGN_CENTER, UI_S(142), UI_S(-142));

    // --- Twin Telemetry Cards ---
    // Left Card: 3-Hour Activity & Closest Distance
    lv_obj_t *card_3h = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_3h);
    lv_obj_set_size(card_3h, UI_S(148), UI_S(64));
    lv_obj_align(card_3h, LV_ALIGN_CENTER, UI_S(-82), UI_S(60));
    lv_obj_set_style_bg_color(card_3h, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_3h, 220, 0);
    lv_obj_set_style_radius(card_3h, UI_S(16), 0);
    lv_obj_set_style_border_color(card_3h, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_3h, 1, 0);

    lv_obj_t *hdr_3h = lv_label_create(card_3h);
    lv_obj_set_style_text_font(hdr_3h, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_3h, COL_SWEEP, 0);
    lv_label_set_text(hdr_3h, "RECENT (3H)");
    lv_obj_align(hdr_3h, LV_ALIGN_TOP_MID, 0, UI_S(5));

    s_cnt3h_lbl = lv_label_create(card_3h);
    lv_obj_set_style_text_font(s_cnt3h_lbl, SCREEN_W >= 600 ? &lv_font_montserrat_24 : &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_cnt3h_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_cnt3h_lbl, "0 STRIKES");
    lv_obj_align(s_cnt3h_lbl, LV_ALIGN_TOP_MID, 0, UI_S(20));

    s_cat3h_lbl = lv_label_create(card_3h);
    lv_obj_set_style_text_font(s_cat3h_lbl, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_cat3h_lbl, COL_ZONE_GREEN, 0);
    lv_label_set_text(s_cat3h_lbl, "Status: Clear");
    lv_obj_align(s_cat3h_lbl, LV_ALIGN_TOP_MID, 0, UI_S(42));

    // Right Card: Daily Total & Last Detected
    lv_obj_t *card_today = lv_obj_create(s_panel);
    lv_obj_remove_style_all(card_today);
    lv_obj_set_size(card_today, UI_S(148), UI_S(64));
    lv_obj_align(card_today, LV_ALIGN_CENTER, UI_S(82), UI_S(60));
    lv_obj_set_style_bg_color(card_today, COL_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card_today, 220, 0);
    lv_obj_set_style_radius(card_today, UI_S(16), 0);
    lv_obj_set_style_border_color(card_today, COL_CARD_BORDER, 0);
    lv_obj_set_style_border_width(card_today, 1, 0);

    lv_obj_t *hdr_today = lv_label_create(card_today);
    lv_obj_set_style_text_font(hdr_today, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hdr_today, COL_SWEEP, 0);
    lv_label_set_text(hdr_today, "TODAY'S TOTAL");
    lv_obj_align(hdr_today, LV_ALIGN_TOP_MID, 0, UI_S(5));

    s_cnttoday_lbl = lv_label_create(card_today);
    lv_obj_set_style_text_font(s_cnttoday_lbl, SCREEN_W >= 600 ? &lv_font_montserrat_24 : &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_cnttoday_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_cnttoday_lbl, "0 STRIKES");
    lv_obj_align(s_cnttoday_lbl, LV_ALIGN_TOP_MID, 0, UI_S(20));

    s_cattoday_lbl = lv_label_create(card_today);
    lv_obj_set_style_text_font(s_cattoday_lbl, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_cattoday_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_cattoday_lbl, "Last: None");
    lv_obj_align(s_cattoday_lbl, LV_ALIGN_TOP_MID, 0, UI_S(42));

    // Centered Footer (placed cleanly above 5 page dots)
    lv_obj_t *footer = lv_label_create(s_panel);
    lv_obj_set_style_text_font(footer, SCREEN_W >= 600 ? &lv_font_montserrat_16 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x64748B), 0);
    lv_label_set_text(footer, "TEMPEST REAL-TIME SENSOR");
    lv_obj_align(footer, LV_ALIGN_CENTER, 0, UI_S(108));

    // 33ms timer for ultra-smooth radar sweep animation and ripple
    lv_timer_create(lightning_timer_cb, 33, nullptr);

    return s_panel;
}

void screen_lightning_update(const TempestState &state) {
    if (!s_panel) return;

    s_last_dist_km = state.lightning_dist_km;

    // Trigger ripple on fresh strike alert
    if (state.strike_alert_active) {
        s_ripple_active = true;
        s_ripple_radius = 5.0f;
        tempest_clear_strike_alert();
    }

    // Dynamic unit labels on Range Rings
    bool is_metric = (state.units == UNIT_METRIC);
    if (is_metric) {
        lv_label_set_text(s_lbl_r1, "8 km");
        lv_label_set_text(s_lbl_r2, "16 km");
        lv_label_set_text(s_lbl_r3, "32 km");
        lv_label_set_text(s_lbl_r4, "64 km");
    } else {
        lv_label_set_text(s_lbl_r1, "5 mi");
        lv_label_set_text(s_lbl_r2, "10 mi");
        lv_label_set_text(s_lbl_r3, "20 mi");
        lv_label_set_text(s_lbl_r4, "40 mi");
    }

    time_t now = time(nullptr);
    int64_t elapsed_s = (state.last_strike_epoch > 0 && now > state.last_strike_epoch) ?
                        (now - state.last_strike_epoch) : -1;

    bool has_recent = (state.lightning_dist_km > 0.0f && elapsed_s >= 0 && elapsed_s < 10800);

    if (has_recent) {
        float d_disp = dist_to_unit(state.lightning_dist_km, state.units);
        const char *u_str = dist_unit_str(state.units);

        char dist_buf[32];
        snprintf(dist_buf, sizeof(dist_buf), "Closest: %.1f %s", d_disp, u_str);
        lv_label_set_text(s_cat3h_lbl, dist_buf);

        if (state.lightning_dist_km < 8.0f) {
            lv_obj_set_style_text_color(s_cat3h_lbl, COL_ZONE_RED, 0);
        } else if (state.lightning_dist_km < 16.0f) {
            lv_obj_set_style_text_color(s_cat3h_lbl, COL_ZONE_AMBER, 0);
        } else {
            lv_obj_set_style_text_color(s_cat3h_lbl, COL_ZONE_YELLOW, 0);
        }
    } else {
        lv_label_set_text(s_cat3h_lbl, "Status: Clear");
        lv_obj_set_style_text_color(s_cat3h_lbl, COL_ZONE_GREEN, 0);
    }

    // 3-Hour Strike Count
    char cnt3h_buf[32];
    snprintf(cnt3h_buf, sizeof(cnt3h_buf), "%d STRIKES", state.lightning_count_3h);
    lv_label_set_text(s_cnt3h_lbl, cnt3h_buf);

    // Today's Total Strike Count
    char cnttoday_buf[32];
    snprintf(cnttoday_buf, sizeof(cnttoday_buf), "%d STRIKES", state.lightning_count_today);
    lv_label_set_text(s_cnttoday_lbl, cnttoday_buf);

    // Last strike timestamp / elapsed in right card
    char last_buf[32];
    if (elapsed_s >= 0 && elapsed_s < 86400) {
        int m = (int)(elapsed_s / 60);
        if (m < 1) {
            snprintf(last_buf, sizeof(last_buf), "Last: < 1m ago");
        } else if (m < 60) {
            snprintf(last_buf, sizeof(last_buf), "Last: %dm ago", m);
        } else {
            snprintf(last_buf, sizeof(last_buf), "Last: %dh ago", m / 60);
        }
    } else {
        snprintf(last_buf, sizeof(last_buf), "Last: None");
    }
    lv_label_set_text(s_cattoday_lbl, last_buf);

    if (s_radar_canvas) lv_obj_invalidate(s_radar_canvas);
}
