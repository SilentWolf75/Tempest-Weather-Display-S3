#include "display.h"
#include "config.h"
#include "settings_mgr.h"
#include "touch_cst9217.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <esp_heap_caps.h>

static Arduino_DataBus *s_bus = nullptr;
static Arduino_CO5300  *s_gfx = nullptr;

#define LVGL_BUF_LINES 32
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;
static lv_color_t        *s_buf1 = nullptr;
static lv_color_t        *s_buf2 = nullptr;
static lv_color_t        *s_rotBuf = nullptr;

#ifndef DEFAULT_ROTATION
#define DEFAULT_ROTATION 270
#endif
static volatile uint16_t s_rot = DEFAULT_ROTATION;

static void draw_block(int16_t x, int16_t y, lv_color_t *pixels, uint16_t w, uint16_t h) {
#if (LV_COLOR_16_SWAP != 0)
    s_gfx->draw16bitBeRGBBitmap(x, y, (uint16_t *)pixels, w, h);
#else
    s_gfx->draw16bitRGBBitmap(x, y, (uint16_t *)pixels, w, h);
#endif
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int16_t  w = (int16_t)(area->x2 - area->x1 + 1);
    int16_t  h = (int16_t)(area->y2 - area->y1 + 1);
    const uint16_t angle = s_rot;

    lv_color_t *out = px;
    int16_t  dx = area->x1, dy = area->y1;
    uint16_t dw = (uint16_t)w, dh = (uint16_t)h;

    switch (angle) {
        case 180:
            for (int i = 0, j = w * h - 1; i < j; ++i, --j) {
                lv_color_t t = px[i]; px[i] = px[j]; px[j] = t;
            }
            dx = (int16_t)(SCREEN_W - 1 - area->x2);
            dy = (int16_t)(SCREEN_H - 1 - area->y2);
            break;
        case 90:
            if (s_rotBuf) {
                for (int j = 0; j < h; ++j) {
                    for (int i = 0; i < w; ++i) {
                        s_rotBuf[i * h + (h - 1 - j)] = px[j * w + i];
                    }
                }
                out = s_rotBuf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = (int16_t)(SCREEN_H - 1 - area->y2); dy = area->x1;
            }
            break;
        case 270:
            if (s_rotBuf) {
                for (int j = 0; j < h; ++j) {
                    for (int i = 0; i < w; ++i) {
                        s_rotBuf[(w - 1 - i) * h + j] = px[j * w + i];
                    }
                }
                out = s_rotBuf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = area->y1; dy = (int16_t)(SCREEN_W - 1 - area->x2);
            }
            break;
        default: break;  // 0°
    }

    draw_block(dx, dy, out, dw, dh);
    lv_disp_flush_ready(drv);
}

// CO5300 QSPI panel requires 2-pixel-aligned flush windows (even start, odd end)
static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    (void)drv;
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    uint16_t x = 0, y = 0;
    if (touch_read(&x, &y)) {
        int lx = x, ly = y;
        const uint16_t angle = s_rot;
        switch (angle) {
            case 90:  lx = y;                ly = SCREEN_H - 1 - x; break;
            case 180: lx = SCREEN_W - 1 - x; ly = SCREEN_H - 1 - y; break;
            case 270: lx = SCREEN_W - 1 - y; ly = x; break;
            default: break;
        }
        if (lx < 0 || lx >= SCREEN_W || ly < 0 || ly >= SCREEN_H) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        data->point.x = (lv_coord_t)lx;
        data->point.y = (lv_coord_t)ly;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

namespace display {

bool begin() {
    Serial.println("[display] Initializing CO5300 QSPI AMOLED...");
    s_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK,
                                  PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    s_gfx = new Arduino_CO5300(s_bus, PIN_LCD_RST, 0 /*rotation*/,
                               SCREEN_W, SCREEN_H,
                               LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);

    if (!s_gfx->begin(LCD_QSPI_HZ)) {
        Serial.println("[display] ERROR: gfx->begin() failed!");
        return false;
    }

    AppSettings s;
    settings_get(&s);
    s_rot = s.screen_rotation;

    s_gfx->fillScreen(RGB565_BLACK);
    s_gfx->setBrightness(s.brightness_day);
    Serial.printf("[display] Panel initialized with rotation %d deg, brightness %d. Setting up LVGL...\n", s_rot, s.brightness_day);

    lv_init();

    const size_t buf_px = (size_t)SCREEN_W * LVGL_BUF_LINES;
    s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_buf1) {
        Serial.println("[display] Internal DMA buf failed; allocating in PSRAM");
        s_buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }

    s_buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_buf2) {
        s_buf2 = nullptr; // single buffer fallback
    }

    s_rotBuf = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, buf_px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res   = SCREEN_W;
    s_disp_drv.ver_res   = SCREEN_H;
    s_disp_drv.flush_cb  = flush_cb;
    s_disp_drv.rounder_cb = rounder_cb;
    s_disp_drv.draw_buf  = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    if (touch_begin()) {
        lv_indev_drv_init(&s_indev_drv);
        s_indev_drv.type = LV_INDEV_TYPE_POINTER;
        s_indev_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_indev_drv);
        Serial.println("[display] CST9217 touch registered with LVGL");
    }

    Serial.printf("[display] LVGL ready. Free PSRAM: %u KB, Heap: %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024),
                  (unsigned)(ESP.getFreeHeap() / 1024));
    return true;
}

void loop() {
    lv_timer_handler();
}

void setBrightness(uint8_t v) {
    if (s_gfx) s_gfx->setBrightness(v);
}

uint32_t inactiveMs() {
    return lv_disp_get_inactive_time(NULL);
}

void setRotation(uint16_t degrees) {
    s_rot = (uint16_t)(degrees % 360);
    if (s_gfx) s_gfx->fillScreen(RGB565_BLACK);
    lv_obj_t *scr = lv_scr_act();
    if (scr) lv_obj_invalidate(scr);
}

uint16_t rotation() {
    return s_rot;
}

} // namespace display
