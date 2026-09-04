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

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_radar_canvas = nullptr;
static lv_obj_t *s_dist_lbl = nullptr;
static lv_obj_t *s_time_lbl = nullptr;
static lv_obj_t *s_count_lbl = nullptr;

static float s_sweep_angle = 0.0f;
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

    // 4 Range Rings: 5 mi / 10 mi / 20 mi / 40 mi
    draw_ring(d, cx, cy, 205, COL_ZONE_GREEN, 1, LV_OPA_30);
    draw_ring(d, cx, cy, 150, COL_ZONE_YELLOW, 1, LV_OPA_40);
    draw_ring(d, cx, cy, 95,  COL_ZONE_AMBER,  1, LV_OPA_50);
    draw_ring(d, cx, cy, 45,  COL_ZONE_RED,    1, LV_OPA_60);

    // Crosshairs
    draw_line(d, cx, cy - 205, cx, cy + 205, COL_RADAR_RING, 1, LV_OPA_30);
    draw_line(d, cx - 205, cy, cx + 205, cy, COL_RADAR_RING, 1, LV_OPA_30);

    // Center station position dot
    fill_circle(d, cx, cy, 4, COL_SWEEP, LV_OPA_COVER);

    // Rotating radar sweep arm
    float rad = s_sweep_angle * (float)M_PI / 180.0f;
    lv_coord_t sx = (lv_coord_t)(cx + sinf(rad) * 205);
    lv_coord_t sy = (lv_coord_t)(cy - cosf(rad) * 205);
    draw_line(d, cx, cy, sx, sy, COL_SWEEP, 2, LV_OPA_60);

    // Ripple shockwave if active
    if (s_ripple_active && s_ripple_radius > 0.0f) {
        draw_ring(d, cx, cy, (lv_coord_t)s_ripple_radius, COL_ZONE_RED, 3,
                  (lv_opa_t)(255 * (1.0f - s_ripple_radius / 210.0f)));
    }

    // Strike marker circle if a strike is recorded
    if (s_last_dist_km > 0.0f && s_last_dist_km <= 65.0f) {
        // Map km (0..64km) to radar radius (0..205px)
        lv_coord_t strike_r = (lv_coord_t)((s_last_dist_km / 64.0f) * 205.0f);
        if (strike_r < 15) strike_r = 15;
        if (strike_r > 205) strike_r = 205;

        lv_color_t strike_col = (s_last_dist_km < 8.0f) ? COL_ZONE_RED :
                                ((s_last_dist_km < 24.0f) ? COL_ZONE_AMBER : COL_ZONE_YELLOW);
        draw_ring(d, cx, cy, strike_r, strike_col, 2, LV_OPA_80);
    }
}

static void lightning_timer_cb(lv_timer_t *timer) {
    (void)timer;
    s_sweep_angle += 1.5f;
    if (s_sweep_angle >= 360.0f) s_sweep_angle -= 360.0f;

    if (s_ripple_active) {
        s_ripple_radius += 4.5f;
        if (s_ripple_radius > 210.0f) {
            s_ripple_active = false;
            s_ripple_radius = 0.0f;
        }
    }

    if (s_radar_canvas) lv_obj_invalidate(s_radar_canvas);
}

lv_obj_t* screen_lightning_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Radar canvas object
    s_radar_canvas = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_radar_canvas);
    lv_obj_set_size(s_radar_canvas, SCREEN_W, SCREEN_H);
    lv_obj_center(s_radar_canvas);
    lv_obj_add_event_cb(s_radar_canvas, radar_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_clear_flag(s_radar_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Title (y = 38)
    lv_obj_t *title = lv_label_create(s_panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_SOFT, 0);
    lv_label_set_text(title, "LIGHTNING RADAR");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 38);

    // Concentric Range Ring distance labels on top-right quadrant
    lv_obj_t *lbl_5m = lv_label_create(s_panel);
    lv_obj_set_style_text_font(lbl_5m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_5m, COL_ZONE_RED, 0);
    lv_label_set_text(lbl_5m, "5m");
    lv_obj_align(lbl_5m, LV_ALIGN_CENTER, 36, -34);

    lv_obj_t *lbl_10m = lv_label_create(s_panel);
    lv_obj_set_style_text_font(lbl_10m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_10m, COL_ZONE_AMBER, 0);
    lv_label_set_text(lbl_10m, "10m");
    lv_obj_align(lbl_10m, LV_ALIGN_CENTER, 74, -70);

    lv_obj_t *lbl_20m = lv_label_create(s_panel);
    lv_obj_set_style_text_font(lbl_20m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_20m, COL_ZONE_YELLOW, 0);
    lv_label_set_text(lbl_20m, "20m");
    lv_obj_align(lbl_20m, LV_ALIGN_CENTER, 114, -110);

    lv_obj_t *lbl_40m = lv_label_create(s_panel);
    lv_obj_set_style_text_font(lbl_40m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_40m, COL_ZONE_GREEN, 0);
    lv_label_set_text(lbl_40m, "40m");
    lv_obj_align(lbl_40m, LV_ALIGN_CENTER, 155, -150);

    // Center Strike Distance readout (y = 190)
    s_dist_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_dist_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_dist_lbl, COL_ZONE_GREEN, 0);
    lv_obj_set_style_text_align(s_dist_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_dist_lbl, "No Strikes Detected");
    lv_obj_align(s_dist_lbl, LV_ALIGN_CENTER, 0, 5);

    // Time since last strike (y = 230)
    s_time_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_time_lbl, COL_TEXT_MAIN, 0);
    lv_obj_set_style_text_align(s_time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_time_lbl, "Clear for 3+ hours");
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 36);

    // Strike count stats (y = 310)
    s_count_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_count_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_align(s_count_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_count_lbl, "Strikes (3h): 0  |  Today: 0");
    lv_obj_align(s_count_lbl, LV_ALIGN_CENTER, 0, 95);

    // 40ms timer for sweep animation and ripple
    lv_timer_create(lightning_timer_cb, 40, nullptr);

    return s_panel;
}

void screen_lightning_update(const TempestState &state) {
    if (!s_panel) return;

    s_last_dist_km = state.lightning_dist_km;

    // Trigger ripple on strike event
    if (state.strike_alert_active) {
        s_ripple_active = true;
        s_ripple_radius = 5.0f;
        tempest_clear_strike_alert();
    }

    time_t now = time(nullptr);
    int64_t elapsed_s = (state.last_strike_epoch > 0 && now > state.last_strike_epoch) ?
                        (now - state.last_strike_epoch) : -1;

    // Display strike proximity
    char dist_buf[48];
    if (state.lightning_dist_km > 0.0f && elapsed_s >= 0 && elapsed_s < 10800) {
        float d_disp = dist_to_unit(state.lightning_dist_km, state.units);
        const char *u_str = dist_unit_str(state.units);

        snprintf(dist_buf, sizeof(dist_buf), "%.1f %s away", d_disp, u_str);
        lv_label_set_text(s_dist_lbl, dist_buf);

        if (state.lightning_dist_km < 8.0f) {
            lv_obj_set_style_text_color(s_dist_lbl, COL_ZONE_RED, 0);
        } else if (state.lightning_dist_km < 24.0f) {
            lv_obj_set_style_text_color(s_dist_lbl, COL_ZONE_AMBER, 0);
        } else {
            lv_obj_set_style_text_color(s_dist_lbl, COL_ZONE_YELLOW, 0);
        }

        // Time elapsed
        char time_buf[48];
        int mins = (int)(elapsed_s / 60);
        if (mins < 1) {
            snprintf(time_buf, sizeof(time_buf), "Just now (< 1 min ago)");
        } else if (mins < 60) {
            snprintf(time_buf, sizeof(time_buf), "%d min ago", mins);
        } else {
            snprintf(time_buf, sizeof(time_buf), "%d hr %d min ago", mins / 60, mins % 60);
        }
        lv_label_set_text(s_time_lbl, time_buf);
    } else {
        lv_label_set_text(s_dist_lbl, "No Strikes Detected");
        lv_obj_set_style_text_color(s_dist_lbl, COL_ZONE_GREEN, 0);
        lv_label_set_text(s_time_lbl, "Clear for past 3+ hours");
    }

    // Strike count
    char cnt_buf[64];
    snprintf(cnt_buf, sizeof(cnt_buf), "Strikes (3h): %d  |  Today: %d",
             state.lightning_count_3h, state.lightning_count_today);
    lv_label_set_text(s_count_lbl, cnt_buf);
}
