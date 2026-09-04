#include "weather_anim.h"
#include <Arduino.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =========================================================================
//  High-End AMOLED Weather Palette
// =========================================================================
#define COL_SUN_CORE        lv_color_hex(0xFF9800) // Deep warm amber-orange
#define COL_SUN_HIGHLIGHT   lv_color_hex(0xFFEB3B) // Radiant specular yellow
#define COL_SUN_GLOW        lv_color_hex(0xFBBF24) // Mid corona halo
#define COL_SUN_AURA        lv_color_hex(0xF59E0B) // Atmospheric outer aura

#define COL_MOON_BODY       lv_color_hex(0xF1F5F9) // Luminous pearl silver
#define COL_MOON_SHADE      lv_color_hex(0x94A3B8) // Lunar crater shading
#define COL_STAR            lv_color_hex(0xFEF08A) // Brilliant starlight

#define COL_CLOUD_SHADOW    lv_color_hex(0x1E293B) // Deep volumetric ambient drop shadow
#define COL_CLOUD_BASE      lv_color_hex(0x475569) // Shaded cloud underbelly
#define COL_CLOUD_BODY      lv_color_hex(0xCBD5E1) // Soft fluffy cloud body
#define COL_CLOUD_HIGHLIGHT lv_color_hex(0xFFFFFF) // Specular top rim sunlight

#define COL_STORM_SHADOW    lv_color_hex(0x0F172A) // Heavy thundercloud base
#define COL_STORM_BODY      lv_color_hex(0x334155) // Dark slate storm body
#define COL_STORM_FLASH     lv_color_hex(0xFDE047) // Internal lightning flash glow

#define COL_RAIN_CYAN       lv_color_hex(0x38BDF8) // Electric cyan drop head
#define COL_RAIN_TRAIL      lv_color_hex(0x0284C7) // Fading streak body
#define COL_SPLASH          lv_color_hex(0x7DD3FC) // Water impact ripple

#define COL_LIGHTNING_CORE  lv_color_hex(0xFFFFFF) // Hot white arc core
#define COL_LIGHTNING_GLOW  lv_color_hex(0xFACC15) // Neon yellow lightning aura

#define COL_SNOW_FLAKE      lv_color_hex(0xF8FAFC) // Crisp crystalline snow
#define COL_WIND_STREAM     lv_color_hex(0x94A3B8) // Aerodynamic wind ribbon

enum WeatherKind {
    WK_CLEAR_DAY,
    WK_CLEAR_NIGHT,
    WK_PARTLY_DAY,
    WK_PARTLY_NIGHT,
    WK_CLOUDY,
    WK_FOG,
    WK_RAIN,
    WK_THUNDERSTORM,
    WK_SNOW,
    WK_WIND
};

struct AnimParticle {
    float x_rel;   // -1.0 to 1.0 relative to cloud width
    float y_prog;  // 0.0 to 1.0 fall progression
    float speed;
    float length;
};

struct WeatherAnimData {
    WeatherKind   kind;
    float         sun_rot_deg;
    float         pulse_phase;
    float         cloud_phase;
    AnimParticle  particles[6];
    int           lightning_frame;
    int           lightning_next_flash;
    lv_timer_t   *timer;
};

static WeatherKind parse_slug(const char *slug) {
    if (!slug) return WK_PARTLY_DAY;
    if (strstr(slug, "thunderstorm")) return WK_THUNDERSTORM;
    if (strstr(slug, "rain") || strstr(slug, "sleet") || strstr(slug, "drizzle")) return WK_RAIN;
    if (strstr(slug, "snow")) return WK_SNOW;
    if (strstr(slug, "fog") || strstr(slug, "mist")) return WK_FOG;
    if (strstr(slug, "wind")) return WK_WIND;
    if (strstr(slug, "partly-cloudy-night")) return WK_PARTLY_NIGHT;
    if (strstr(slug, "partly-cloudy")) return WK_PARTLY_DAY;
    if (strstr(slug, "night") || strstr(slug, "moon")) return WK_CLEAR_NIGHT;
    if (strstr(slug, "cloud")) return WK_CLOUDY;
    return WK_CLEAR_DAY;
}

// =========================================================================
//  Vector Graphics Primitives
// =========================================================================
static void fill_circle(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t r,
                        lv_color_t c, lv_opa_t opa) {
    if (r <= 0) return;
    lv_draw_rect_dsc_t s;
    lv_draw_rect_dsc_init(&s);
    s.bg_color = c;
    s.bg_opa = opa;
    s.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = { (lv_coord_t)(cx - r), (lv_coord_t)(cy - r),
                    (lv_coord_t)(cx + r), (lv_coord_t)(cy + r) };
    lv_draw_rect(d, &s, &a);
}

static void draw_pill(lv_draw_ctx_t *d, lv_coord_t x1, lv_coord_t y1,
                      lv_coord_t x2, lv_coord_t y2, lv_color_t c,
                      lv_coord_t w, lv_opa_t opa) {
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

// 4-Point Micro Diamond Twinkle Star
static void draw_star(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy,
                      lv_coord_t size, lv_opa_t opa) {
    draw_pill(d, cx - size, cy, cx + size, cy, COL_STAR, 2, opa);
    draw_pill(d, cx, cy - size, cx, cy + size, COL_STAR, 2, opa);
    fill_circle(d, cx, cy, 1, lv_color_white(), opa);
}

// =========================================================================
//  High-End Sun with Radial Aura, 3D Orb, & Rotating Pill Rays
// =========================================================================
static void draw_sun(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t r,
                     float rot_deg, float pulse, bool compact) {
    // 1. Multi-layered breathing atmospheric aura
    float p_sin = sinf(pulse);
    lv_coord_t aura_r = r + (lv_coord_t)(compact ? (4.0f + 2.0f * p_sin) : (9.0f + 3.0f * p_sin));
    fill_circle(d, cx, cy, aura_r + 4, COL_SUN_AURA, LV_OPA_20);
    fill_circle(d, cx, cy, aura_r,     COL_SUN_GLOW, LV_OPA_40);

    // 2. Radiating rounded pill rays
    const int num_rays = 8;
    lv_coord_t ray_w = compact ? 3 : 4;
    for (int i = 0; i < num_rays; ++i) {
        float angle = (rot_deg + (i * 360.0f / num_rays)) * (float)M_PI / 180.0f;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);

        lv_coord_t r_in  = r + (compact ? 3 : 5);
        lv_coord_t r_out = r + (compact ? 9 : 14) + (lv_coord_t)(2.0f * sinf(pulse + i * 0.8f));

        draw_pill(d,
                  (lv_coord_t)(cx + cos_a * r_in),  (lv_coord_t)(cy + sin_a * r_in),
                  (lv_coord_t)(cx + cos_a * r_out), (lv_coord_t)(cy + sin_a * r_out),
                  COL_SUN_HIGHLIGHT, ray_w, LV_OPA_90);
    }

    // 3. Sun 3D Core: Rich amber foundation
    fill_circle(d, cx, cy, r, COL_SUN_CORE, LV_OPA_COVER);

    // 4. Specular volumetric orb highlight (light source top-left)
    lv_coord_t hl_r1 = (lv_coord_t)(r * 0.55f);
    lv_coord_t hl_r2 = (lv_coord_t)(r * 0.25f);
    fill_circle(d, (lv_coord_t)(cx - r * 0.28f), (lv_coord_t)(cy - r * 0.28f),
                hl_r1, COL_SUN_HIGHLIGHT, LV_OPA_70);
    fill_circle(d, (lv_coord_t)(cx - r * 0.35f), (lv_coord_t)(cy - r * 0.35f),
                hl_r2, lv_color_white(), LV_OPA_90);
}

// =========================================================================
//  High-End Crescent Moon with Lunar Glow & Craters
// =========================================================================
static void draw_moon(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t r) {
    // Soft lunar aura
    fill_circle(d, cx, cy, r + 4, COL_MOON_BODY, LV_OPA_20);

    // Luminous pearl disc
    fill_circle(d, cx, cy, r, COL_MOON_BODY, LV_OPA_COVER);

    // Subtle crater shading before punch-out
    fill_circle(d, (lv_coord_t)(cx - r * 0.25f), (lv_coord_t)(cy - r * 0.15f),
                (lv_coord_t)(r * 0.20f), COL_MOON_SHADE, LV_OPA_40);
    fill_circle(d, (lv_coord_t)(cx - r * 0.10f), (lv_coord_t)(cy + r * 0.28f),
                (lv_coord_t)(r * 0.16f), COL_MOON_SHADE, LV_OPA_40);

    // Crescent punch-out disc
    fill_circle(d, (lv_coord_t)(cx + r * 0.48f), (lv_coord_t)(cy - r * 0.28f),
                (lv_coord_t)(r * 0.85f), lv_color_black(), LV_OPA_COVER);
}

// =========================================================================
//  High-End 3D Volumetric Cloud with Rim Highlights & Ambient Shading
// =========================================================================
static void draw_volumetric_cloud(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy,
                                  lv_coord_t s, bool is_storm, lv_opa_t flash_opa) {
    // Lobes geometry (relative offsets and radii, strictly kept within bounds)
    // 5 billowy domes + rounded pill base slab
    const lv_coord_t r_center = (lv_coord_t)(s * 0.28f);
    const lv_coord_t r_top_l  = (lv_coord_t)(s * 0.22f);
    const lv_coord_t r_far_l  = (lv_coord_t)(s * 0.17f);
    const lv_coord_t r_top_r  = (lv_coord_t)(s * 0.20f);
    const lv_coord_t r_far_r  = (lv_coord_t)(s * 0.15f);

    const lv_coord_t dx_c  = (lv_coord_t)(s * 0.02f);
    const lv_coord_t dy_c  = (lv_coord_t)(-s * 0.10f);

    const lv_coord_t dx_tl = (lv_coord_t)(-s * 0.20f);
    const lv_coord_t dy_tl = (lv_coord_t)(-s * 0.02f);

    const lv_coord_t dx_fl = (lv_coord_t)(-s * 0.35f);
    const lv_coord_t dy_fl = (lv_coord_t)(s * 0.10f);

    const lv_coord_t dx_tr = (lv_coord_t)(s * 0.20f);
    const lv_coord_t dy_tr = (lv_coord_t)(-s * 0.02f);

    const lv_coord_t dx_fr = (lv_coord_t)(s * 0.35f);
    const lv_coord_t dy_fr = (lv_coord_t)(s * 0.11f);

    const lv_coord_t base_y = (lv_coord_t)(cy + s * 0.10f);
    const lv_coord_t base_h = (lv_coord_t)(s * 0.22f);
    const lv_coord_t base_w = (lv_coord_t)(s * 0.35f);

    // --- PASS 1: Volumetric Drop Shadow (Offset down-right) ---
    lv_color_t col_sh = is_storm ? COL_STORM_SHADOW : COL_CLOUD_SHADOW;
    lv_coord_t sh_x = 2, sh_y = 4;

    fill_circle(d, cx + dx_c + sh_x,  cy + dy_c + sh_y,  r_center, col_sh, LV_OPA_60);
    fill_circle(d, cx + dx_tl + sh_x, cy + dy_tl + sh_y, r_top_l,  col_sh, LV_OPA_60);
    fill_circle(d, cx + dx_fl + sh_x, cy + dy_fl + sh_y, r_far_l,  col_sh, LV_OPA_60);
    fill_circle(d, cx + dx_tr + sh_x, cy + dy_tr + sh_y, r_top_r,  col_sh, LV_OPA_60);
    fill_circle(d, cx + dx_fr + sh_x, cy + dy_fr + sh_y, r_far_r,  col_sh, LV_OPA_60);
    draw_pill(d, cx - base_w + sh_x, base_y + sh_y,
                 cx + base_w + sh_x, base_y + sh_y, col_sh, base_h, LV_OPA_60);

    // --- PASS 2: Ambient Shaded Underbelly ---
    lv_color_t col_base = is_storm ? COL_STORM_BODY : COL_CLOUD_BASE;
    draw_pill(d, cx - base_w, base_y + 2,
                 cx + base_w, base_y + 2, col_base, base_h, LV_OPA_COVER);

    // --- PASS 3: Main Volumetric Cloud Body ---
    lv_color_t col_body = is_storm ? COL_STORM_BODY : COL_CLOUD_BODY;
    if (flash_opa > 0) {
        col_body = COL_STORM_FLASH; // Illuminated from within during lightning
    }

    fill_circle(d, cx + dx_c,  cy + dy_c,  r_center, col_body, LV_OPA_COVER);
    fill_circle(d, cx + dx_tl, cy + dy_tl, r_top_l,  col_body, LV_OPA_COVER);
    fill_circle(d, cx + dx_fl, cy + dy_fl, r_far_l,  col_body, LV_OPA_COVER);
    fill_circle(d, cx + dx_tr, cy + dy_tr, r_top_r,  col_body, LV_OPA_COVER);
    fill_circle(d, cx + dx_fr, cy + dy_fr, r_far_r,  col_body, LV_OPA_COVER);
    draw_pill(d, cx - base_w, base_y,
                 cx + base_w, base_y, col_body, base_h, LV_OPA_COVER);

    // --- PASS 4: Specular Rim Highlights (Top sunlight reflection) ---
    lv_color_t col_hl = COL_CLOUD_HIGHLIGHT;
    lv_opa_t opa_hl = is_storm ? LV_OPA_40 : LV_OPA_80;

    // Center lobe highlight cap
    fill_circle(d, cx + dx_c - 2, cy + dy_c - (lv_coord_t)(r_center * 0.35f),
                (lv_coord_t)(r_center * 0.70f), col_hl, opa_hl);
    // Left lobe highlight cap
    fill_circle(d, cx + dx_tl - 2, cy + dy_tl - (lv_coord_t)(r_top_l * 0.35f),
                (lv_coord_t)(r_top_l * 0.68f), col_hl, opa_hl);
    // Right lobe highlight cap
    fill_circle(d, cx + dx_tr - 1, cy + dy_tr - (lv_coord_t)(r_top_r * 0.35f),
                (lv_coord_t)(r_top_r * 0.65f), col_hl, opa_hl);
}

// =========================================================================
//  High-Voltage Jagged Lightning Arc
// =========================================================================
static void draw_lightning(lv_draw_ctx_t *d, lv_coord_t cx, lv_coord_t cy, lv_coord_t size) {
    lv_coord_t x0 = cx - (lv_coord_t)(size * 0.15f);
    lv_coord_t y0 = cy - (lv_coord_t)(size * 0.45f);

    lv_coord_t x1 = cx + (lv_coord_t)(size * 0.12f);
    lv_coord_t y1 = cy;

    lv_coord_t x2 = cx - (lv_coord_t)(size * 0.08f);
    lv_coord_t y2 = cy + (lv_coord_t)(size * 0.10f);

    lv_coord_t x3 = cx + (lv_coord_t)(size * 0.25f);
    lv_coord_t y3 = cy + (lv_coord_t)(size * 0.55f);

    // Fork branch
    lv_coord_t xb = cx + (lv_coord_t)(size * 0.32f);
    lv_coord_t yb = cy + (lv_coord_t)(size * 0.28f);

    // 1. Neon glowing electric aura
    draw_pill(d, x0, y0, x1, y1, COL_LIGHTNING_GLOW, 6, LV_OPA_80);
    draw_pill(d, x1, y1, x2, y2, COL_LIGHTNING_GLOW, 6, LV_OPA_80);
    draw_pill(d, x2, y2, x3, y3, COL_LIGHTNING_GLOW, 6, LV_OPA_80);
    draw_pill(d, x1, y1, xb, yb, COL_LIGHTNING_GLOW, 4, LV_OPA_70);

    // 2. Hot white electric core
    draw_pill(d, x0, y0, x1, y1, COL_LIGHTNING_CORE, 3, LV_OPA_COVER);
    draw_pill(d, x1, y1, x2, y2, COL_LIGHTNING_CORE, 3, LV_OPA_COVER);
    draw_pill(d, x2, y2, x3, y3, COL_LIGHTNING_CORE, 3, LV_OPA_COVER);
    draw_pill(d, x1, y1, xb, yb, COL_LIGHTNING_CORE, 2, LV_OPA_COVER);
}

// =========================================================================
//  Main LVGL Draw Callback
// =========================================================================
static void weather_anim_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *d = lv_event_get_draw_ctx(e);
    WeatherAnimData *ad = (WeatherAnimData *)lv_obj_get_user_data(obj);
    if (!ad) return;

    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    lv_coord_t w = lv_area_get_width(&box);
    lv_coord_t h = lv_area_get_height(&box);
    lv_coord_t cx = (lv_coord_t)(box.x1 + w / 2);
    lv_coord_t cy = (lv_coord_t)(box.y1 + h / 2);

    // Scale parameter bounded to ensure safety margin against container edges
    // With w=130, s=94. Max radius = 47. 65 - 47 = 18px padding on every side!
    lv_coord_t s = (lv_coord_t)((w < h ? w : h) * 0.72f);

    // Organic floating sine wave bobbing
    lv_coord_t cloud_drift_x = (lv_coord_t)(2.2f * sinf(ad->cloud_phase));
    lv_coord_t cloud_drift_y = (lv_coord_t)(1.6f * cosf(ad->cloud_phase * 0.8f));

    switch (ad->kind) {
        case WK_CLEAR_DAY:
            draw_sun(d, cx, cy, (lv_coord_t)(s * 0.30f), ad->sun_rot_deg, ad->pulse_phase, false);
            break;

        case WK_CLEAR_NIGHT:
            // Twinkling micro stars around crescent
            draw_star(d, (lv_coord_t)(cx - s * 0.34f), (lv_coord_t)(cy - s * 0.28f), 4,
                      (lv_opa_t)(140 + 115 * sinf(ad->pulse_phase)));
            draw_star(d, (lv_coord_t)(cx + s * 0.32f), (lv_coord_t)(cy + s * 0.24f), 3,
                      (lv_opa_t)(140 + 115 * cosf(ad->pulse_phase + 1.2f)));
            draw_star(d, (lv_coord_t)(cx - s * 0.24f), (lv_coord_t)(cy + s * 0.32f), 3,
                      (lv_opa_t)(130 + 110 * sinf(ad->pulse_phase * 1.5f)));

            draw_moon(d, (lv_coord_t)(cx + s * 0.04f), cy, (lv_coord_t)(s * 0.34f));
            break;

        case WK_PARTLY_DAY:
            // Sun radiating and rotating behind the cloud in the upper-right
            draw_sun(d, (lv_coord_t)(cx + s * 0.20f), (lv_coord_t)(cy - s * 0.18f),
                     (lv_coord_t)(s * 0.20f), ad->sun_rot_deg, ad->pulse_phase, true);
            // Volumetric puffy cloud floating in foreground
            draw_volumetric_cloud(d, (lv_coord_t)(cx - s * 0.04f + cloud_drift_x),
                                     (lv_coord_t)(cy + s * 0.12f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.86f), false, 0);
            break;

        case WK_PARTLY_NIGHT:
            // Twinkling star
            draw_star(d, (lv_coord_t)(cx - s * 0.32f), (lv_coord_t)(cy - s * 0.24f), 3,
                      (lv_opa_t)(150 + 100 * sinf(ad->pulse_phase)));
            // Crescent moon behind cloud in upper-right
            draw_moon(d, (lv_coord_t)(cx + s * 0.20f), (lv_coord_t)(cy - s * 0.18f),
                      (lv_coord_t)(s * 0.22f));
            // Volumetric puffy cloud floating in foreground
            draw_volumetric_cloud(d, (lv_coord_t)(cx - s * 0.04f + cloud_drift_x),
                                     (lv_coord_t)(cy + s * 0.12f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.86f), false, 0);
            break;

        case WK_CLOUDY:
            // Background darker volumetric shadow cloud
            draw_volumetric_cloud(d, (lv_coord_t)(cx + s * 0.08f - cloud_drift_x),
                                     (lv_coord_t)(cy - s * 0.08f - cloud_drift_y),
                                  (lv_coord_t)(s * 0.80f), true, 0);
            // Foreground bright volumetric cloud
            draw_volumetric_cloud(d, (lv_coord_t)(cx - s * 0.05f + cloud_drift_x),
                                     (lv_coord_t)(cy + s * 0.08f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.86f), false, 0);
            break;

        case WK_FOG:
            for (int i = 0; i < 4; ++i) {
                lv_coord_t fy = (lv_coord_t)(cy - s * 0.25f + i * (s * 0.18f));
                lv_coord_t fx_offset = (lv_coord_t)(4.0f * sinf(ad->cloud_phase + i * 1.3f));
                draw_pill(d, (lv_coord_t)(cx - s * 0.35f + fx_offset), fy,
                             (lv_coord_t)(cx + s * 0.35f + fx_offset), fy,
                          COL_CLOUD_BODY, 4, (lv_opa_t)(140 + i * 28));
            }
            break;

        case WK_RAIN:
            // Stormy raincloud
            draw_volumetric_cloud(d, (lv_coord_t)(cx + cloud_drift_x),
                                     (lv_coord_t)(cy - s * 0.10f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.85f), true, 0);

            // Falling angled raindrops with fading streaks
            for (int i = 0; i < 6; ++i) {
                lv_coord_t rx = (lv_coord_t)(cx + ad->particles[i].x_rel * (s * 0.32f));
                lv_coord_t ry = (lv_coord_t)(cy + s * 0.04f + ad->particles[i].y_prog * (s * 0.40f));
                lv_coord_t rlen = (lv_coord_t)(ad->particles[i].length);

                // Raindrop streak (wind-slanted by 3px)
                draw_pill(d, rx, ry, (lv_coord_t)(rx - 3), (lv_coord_t)(ry + rlen),
                          COL_RAIN_CYAN, 3, (lv_opa_t)(220 * (1.0f - ad->particles[i].y_prog * 0.35f)));

                // Impact ripple splash at the bottom
                if (ad->particles[i].y_prog > 0.82f) {
                    lv_coord_t sp_w = (lv_coord_t)(5.0f * ((ad->particles[i].y_prog - 0.82f) / 0.18f));
                    draw_pill(d, rx - sp_w, ry + rlen, rx + sp_w, ry + rlen,
                              COL_SPLASH, 2, (lv_opa_t)(180 * (1.0f - (ad->particles[i].y_prog - 0.82f) / 0.18f)));
                }
            }
            break;

        case WK_THUNDERSTORM: {
            lv_opa_t flash_opa = (ad->lightning_frame > 0) ? LV_OPA_COVER : LV_OPA_0;

            // Sky electric discharge flash
            if (flash_opa > 0) {
                fill_circle(d, cx, cy, (lv_coord_t)(s * 0.48f), COL_LIGHTNING_GLOW, LV_OPA_30);
            }

            // Dark menacing thundercloud (internally illuminated during flash)
            draw_volumetric_cloud(d, (lv_coord_t)(cx + cloud_drift_x),
                                     (lv_coord_t)(cy - s * 0.12f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.88f), true, flash_opa);

            if (ad->lightning_frame > 0) {
                // High-voltage lightning bolt discharge
                draw_lightning(d, cx, (lv_coord_t)(cy + s * 0.15f), (lv_coord_t)(s * 0.42f));
            } else {
                // Torrential rain between strikes
                for (int i = 0; i < 4; ++i) {
                    lv_coord_t rx = (lv_coord_t)(cx + ad->particles[i].x_rel * (s * 0.30f));
                    lv_coord_t ry = (lv_coord_t)(cy + s * 0.08f + ad->particles[i].y_prog * (s * 0.36f));
                    draw_pill(d, rx, ry, (lv_coord_t)(rx - 3), (lv_coord_t)(ry + 8),
                              COL_RAIN_CYAN, 3, LV_OPA_90);
                }
            }
            break;
        }

        case WK_SNOW:
            draw_volumetric_cloud(d, (lv_coord_t)(cx + cloud_drift_x),
                                     (lv_coord_t)(cy - s * 0.10f + cloud_drift_y),
                                  (lv_coord_t)(s * 0.85f), false, 0);

            // Floating tumbling snowflakes
            for (int i = 0; i < 6; ++i) {
                float sway = sinf(ad->cloud_phase * 2.0f + i * 1.2f) * 3.5f;
                lv_coord_t sx = (lv_coord_t)(cx + ad->particles[i].x_rel * (s * 0.32f) + sway);
                lv_coord_t sy = (lv_coord_t)(cy + s * 0.06f + ad->particles[i].y_prog * (s * 0.38f));

                if (i % 2 == 0) {
                    // Crystalline snowflake
                    draw_star(d, sx, sy, 3, LV_OPA_COVER);
                } else {
                    // Soft snow pellet
                    fill_circle(d, sx, sy, 2, COL_SNOW_FLAKE, LV_OPA_COVER);
                }
            }
            break;

        case WK_WIND:
            for (int i = 0; i < 3; ++i) {
                lv_coord_t wy = (lv_coord_t)(cy - s * 0.20f + i * (s * 0.20f));
                float wave = sinf(ad->cloud_phase + i * 1.5f) * (s * 0.10f);
                draw_pill(d, (lv_coord_t)(cx - s * 0.35f + wave), wy,
                             (lv_coord_t)(cx + s * 0.35f + wave), wy,
                          COL_WIND_STREAM, 3, (lv_opa_t)(160 + i * 35));
            }
            break;
    }
}

// =========================================================================
//  Animation Timer Callback
// =========================================================================
static void weather_anim_timer_cb(lv_timer_t *timer) {
    lv_obj_t *obj = (lv_obj_t *)timer->user_data;
    WeatherAnimData *ad = (WeatherAnimData *)lv_obj_get_user_data(obj);
    if (!ad) return;

    // Advance continuous animations smoothly
    ad->sun_rot_deg += 0.6f;
    if (ad->sun_rot_deg >= 360.0f) ad->sun_rot_deg -= 360.0f;

    ad->pulse_phase += 0.045f;
    if (ad->pulse_phase >= 2.0f * (float)M_PI) ad->pulse_phase -= 2.0f * (float)M_PI;

    ad->cloud_phase += 0.030f;
    if (ad->cloud_phase >= 2.0f * (float)M_PI) ad->cloud_phase -= 2.0f * (float)M_PI;

    // Particle progression (rain, snow)
    for (int i = 0; i < 6; ++i) {
        ad->particles[i].y_prog += ad->particles[i].speed;
        if (ad->particles[i].y_prog > 1.0f) {
            ad->particles[i].y_prog = 0.0f;
        }
    }

    // Lightning discharge state machine
    if (ad->kind == WK_THUNDERSTORM) {
        if (ad->lightning_frame > 0) {
            ad->lightning_frame--;
        } else {
            ad->lightning_next_flash--;
            if (ad->lightning_next_flash <= 0) {
                ad->lightning_frame = 4; // Flash for 4 ticks (~130ms)
                ad->lightning_next_flash = 55 + (rand() % 65); // Next strike in 2.5 - 4.5s
            }
        }
    }

    lv_obj_invalidate(obj);
}

// =========================================================================
//  Public API
// =========================================================================
lv_obj_t* weather_anim_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    WeatherAnimData *ad = (WeatherAnimData *)malloc(sizeof(WeatherAnimData));
    memset(ad, 0, sizeof(WeatherAnimData));
    ad->kind = WK_PARTLY_DAY;
    ad->sun_rot_deg = 0.0f;
    ad->pulse_phase = 0.0f;
    ad->cloud_phase = 0.0f;
    ad->lightning_next_flash = 45;

    // Staggered particle positions and speeds
    for (int i = 0; i < 6; ++i) {
        ad->particles[i].x_rel = -0.7f + i * 0.28f;
        ad->particles[i].y_prog = (float)(i * 0.16f);
        ad->particles[i].speed = 0.045f + (float)(i % 3) * 0.015f;
        ad->particles[i].length = 9.0f + (float)(i % 2) * 4.0f;
    }

    lv_obj_set_user_data(obj, ad);
    lv_obj_add_event_cb(obj, weather_anim_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);

    // 33ms timer = ~30 FPS smooth animation
    ad->timer = lv_timer_create(weather_anim_timer_cb, 33, obj);

    return obj;
}

void weather_anim_set_icon(lv_obj_t *obj, const char *icon_slug) {
    if (!obj) return;
    WeatherAnimData *ad = (WeatherAnimData *)lv_obj_get_user_data(obj);
    if (!ad) return;

    WeatherKind new_kind = parse_slug(icon_slug);
    if (ad->kind != new_kind) {
        ad->kind = new_kind;
        lv_obj_invalidate(obj);
    }
}

void weather_anim_stop(lv_obj_t *obj) {
    if (!obj) return;
    WeatherAnimData *ad = (WeatherAnimData *)lv_obj_get_user_data(obj);
    if (ad && ad->timer) {
        lv_timer_pause(ad->timer);
    }
}

void weather_anim_start(lv_obj_t *obj) {
    if (!obj) return;
    WeatherAnimData *ad = (WeatherAnimData *)lv_obj_get_user_data(obj);
    if (ad && ad->timer) {
        lv_timer_resume(ad->timer);
    }
}
