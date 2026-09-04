#include "screen_wind.h"
#include "config.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

LV_FONT_DECLARE(montserrat_64_digits);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define COL_BG          lv_color_black()
#define COL_NEEDLE_TIP  lv_color_hex(0x38BDF8) // cyan
#define COL_NEEDLE_TAIL lv_color_hex(0x475569) // slate
#define COL_DIAL_RING   lv_color_hex(0x1E293B)
#define COL_TEXT_MAIN   lv_color_hex(0xF8FAFC)
#define COL_TEXT_SOFT   lv_color_hex(0x94A3B8)
#define COL_AMBER       lv_color_hex(0xF59E0B)
#define COL_GREEN       lv_color_hex(0x10B981)

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_compass_canvas = nullptr;
static lv_obj_t *s_speed_lbl = nullptr;
static lv_obj_t *s_unit_lbl = nullptr;
static lv_obj_t *s_dir_lbl = nullptr;
static lv_obj_t *s_stats_lbl = nullptr;

static float s_current_angle = 0.0f;
static float s_target_angle = 0.0f;

static void draw_line(lv_draw_ctx_t *d, lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2,
                      lv_color_t c, lv_coord_t w, lv_opa_t opa) {
    lv_draw_line_dsc_t s;
    lv_draw_line_dsc_init(&s);
    s.color = c;
    s.width = w;
    s.opa = opa;
    s.round_start = 1;
    s.round_end = 1;
    lv_point_t p1 = { x1, y1 }, p2 = { x2, y2 };
    lv_draw_line(d, &s, &p1, &p2);
}

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

static void compass_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *d = lv_event_get_draw_ctx(e);

    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_coord_t cx = (lv_coord_t)(box.x1 + lv_area_get_width(&box) / 2);
    lv_coord_t cy = (lv_coord_t)(box.y1 + lv_area_get_height(&box) / 2);
    lv_coord_t radius = 215;

    // Outer subtle dial ring
    fill_circle(d, cx, cy, radius, COL_DIAL_RING, LV_OPA_30);
    fill_circle(d, cx, cy, radius - 2, COL_BG, LV_OPA_COVER);

    // Compass dial tick marks every 10 degrees (longer at 30 degrees)
    for (int deg = 0; deg < 360; deg += 10) {
        float rad = (float)deg * (float)M_PI / 180.0f;
        float cos_a = cosf(rad);
        float sin_a = sinf(rad);

        bool is_cardinal = (deg % 90 == 0);
        bool is_major = (deg % 30 == 0);

        lv_coord_t r_outer = radius - 4;
        lv_coord_t r_inner = r_outer - (is_cardinal ? 14 : (is_major ? 9 : 4));
        lv_color_t col = is_cardinal ? COL_NEEDLE_TIP : (is_major ? COL_TEXT_MAIN : COL_DIAL_RING);
        lv_coord_t w = is_cardinal ? 3 : (is_major ? 2 : 1);
        lv_opa_t opa = is_cardinal ? LV_OPA_COVER : (is_major ? LV_OPA_80 : LV_OPA_40);

        draw_line(d,
                  (lv_coord_t)(cx + sin_a * r_inner), (lv_coord_t)(cy - cos_a * r_inner),
                  (lv_coord_t)(cx + sin_a * r_outer), (lv_coord_t)(cy - cos_a * r_outer),
                  col, w, opa);
    }

    // Rotating Needle: points to wind origin direction
    float needle_rad = s_current_angle * (float)M_PI / 180.0f;
    float n_cos = cosf(needle_rad);
    float n_sin = sinf(needle_rad);

    lv_coord_t tip_r = radius - 22;
    lv_coord_t tail_r = radius - 80;

    // Needle pointer
    draw_line(d,
              (lv_coord_t)(cx + n_sin * (tip_r - 40)), (lv_coord_t)(cy - n_cos * (tip_r - 40)),
              (lv_coord_t)(cx + n_sin * tip_r), (lv_coord_t)(cy - n_cos * tip_r),
              COL_NEEDLE_TIP, 5, LV_OPA_COVER);

    // Arrowhead tip dot
    fill_circle(d, (lv_coord_t)(cx + n_sin * tip_r), (lv_coord_t)(cy - n_cos * tip_r),
                6, COL_NEEDLE_TIP, LV_OPA_COVER);

    // Opposing tail
    draw_line(d,
              (lv_coord_t)(cx - n_sin * (tail_r - 20)), (lv_coord_t)(cy + n_cos * (tail_r - 20)),
              (lv_coord_t)(cx - n_sin * tail_r), (lv_coord_t)(cy + n_cos * tail_r),
              COL_NEEDLE_TAIL, 3, LV_OPA_60);
}

static void wind_timer_cb(lv_timer_t *timer) {
    (void)timer;
    // Smooth angular interpolation for needle
    float diff = s_target_angle - s_current_angle;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;

    if (fabsf(diff) > 0.3f) {
        s_current_angle += diff * 0.18f;
        if (s_current_angle >= 360.0f) s_current_angle -= 360.0f;
        if (s_current_angle < 0.0f) s_current_angle += 360.0f;
        if (s_compass_canvas) lv_obj_invalidate(s_compass_canvas);
    }
}

lv_obj_t* screen_wind_create(lv_obj_t *parent) {
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, SCREEN_W, SCREEN_H);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Compass rendering canvas object
    s_compass_canvas = lv_obj_create(s_panel);
    lv_obj_remove_style_all(s_compass_canvas);
    lv_obj_set_size(s_compass_canvas, SCREEN_W, SCREEN_H);
    lv_obj_center(s_compass_canvas);
    lv_obj_add_event_cb(s_compass_canvas, compass_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_clear_flag(s_compass_canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Title at Top (y = 42)
    lv_obj_t *title = lv_label_create(s_panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, COL_TEXT_SOFT, 0);
    lv_label_set_text(title, "WIND COMPASS");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    // Cardinal Labels on the Bezel
    lv_obj_t *n_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(n_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(n_lbl, COL_NEEDLE_TIP, 0);
    lv_label_set_text(n_lbl, "N");
    lv_obj_align(n_lbl, LV_ALIGN_CENTER, 0, -178);

    lv_obj_t *s_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(s_lbl, "S");
    lv_obj_align(s_lbl, LV_ALIGN_CENTER, 0, 178);

    lv_obj_t *e_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(e_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(e_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(e_lbl, "E");
    lv_obj_align(e_lbl, LV_ALIGN_CENTER, 178, 0);

    lv_obj_t *w_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(w_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(w_lbl, COL_TEXT_SOFT, 0);
    lv_label_set_text(w_lbl, "W");
    lv_obj_align(w_lbl, LV_ALIGN_CENTER, -178, 0);

    // Giant Wind Speed Digits in Center (y = 175)
    s_speed_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_speed_lbl, &montserrat_64_digits, 0);
    lv_obj_set_style_text_color(s_speed_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_speed_lbl, "0");
    lv_obj_align(s_speed_lbl, LV_ALIGN_CENTER, 0, -28);

    // Units label (y = 230)
    s_unit_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_unit_lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_unit_lbl, COL_NEEDLE_TIP, 0);
    lv_label_set_text(s_unit_lbl, "mph");
    lv_obj_align(s_unit_lbl, LV_ALIGN_CENTER, 0, 14);

    // Direction & Cardinal (y = 260)
    s_dir_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_dir_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_dir_lbl, COL_TEXT_MAIN, 0);
    lv_label_set_text(s_dir_lbl, "NNW  338");
    lv_obj_align(s_dir_lbl, LV_ALIGN_CENTER, 0, 48);

    // Gust / Lull / Avg Stats (y = 330)
    s_stats_lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_stats_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_stats_lbl, COL_TEXT_SOFT, 0);
    lv_obj_set_style_text_align(s_stats_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_stats_lbl, "Gust: 0  |  Lull: 0  |  Avg: 0");
    lv_obj_align(s_stats_lbl, LV_ALIGN_CENTER, 0, 95);

    // 33ms timer for needle smoothing
    lv_timer_create(wind_timer_cb, 33, nullptr);

    return s_panel;
}

void screen_wind_update(const TempestState &state) {
    if (!s_panel) return;

    // Use rapid wind if recent (< 8 seconds), otherwise average wind
    float live_speed = state.rapid_wind_ms;
    int live_dir = state.rapid_wind_dir;

    if (millis() - state.last_packet_millis > 8000) {
        live_speed = state.wind_avg_ms;
        live_dir = state.wind_dir_deg;
    }

    s_target_angle = (float)live_dir;

    float speed_disp = wind_to_unit(live_speed, state.units);
    char spd_buf[16];
    snprintf(spd_buf, sizeof(spd_buf), "%.0f", roundf(speed_disp));
    lv_label_set_text(s_speed_lbl, spd_buf);

    lv_label_set_text(s_unit_lbl, wind_unit_str(state.units));

    char dir_buf[32];
    snprintf(dir_buf, sizeof(dir_buf), "%s  %d deg", wind_cardinal(live_dir), live_dir);
    lv_label_set_text(s_dir_lbl, dir_buf);

    float gust_disp = wind_to_unit(state.wind_gust_ms, state.units);
    float lull_disp = wind_to_unit(state.wind_lull_ms, state.units);
    float avg_disp  = wind_to_unit(state.wind_avg_ms, state.units);
    const char *u_str = wind_unit_str(state.units);

    char stats_buf[64];
    snprintf(stats_buf, sizeof(stats_buf), "Gust: %.0f %s | Lull: %.0f | Avg: %.0f",
             roundf(gust_disp), u_str, roundf(lull_disp), roundf(avg_disp));
    lv_label_set_text(s_stats_lbl, stats_buf);
}
