#pragma once
#include <stdint.h>

// Weather Station Display — Hardware & Network Configuration

#define FW_NAME             "Weather Station Display"
#define FW_VERSION          "1.0.4"
#define MDNS_HOSTNAME       "weather"          // http://weather.local/
#define AP_NAME             "Weather-Display-Setup"
#define DEFAULT_WIFI_SSID   ""
#define DEFAULT_WIFI_PASS   ""

// ---------- Board Selection ----------
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
#  include "boards/waveshare_p4_lcd_4c.h"
#elif defined(BOARD_WAVESHARE_S3_AMOLED_175) || 1
#  include "boards/waveshare_s3_amoled_175.h"
#endif

// ---------- UI Scaling Macro ----------
// Scales 466x466 reference coordinates to any screen size proportionally
#define UI_DESIGN_W         466
#define UI_S(v)             (((int)(v) * SCREEN_W) / UI_DESIGN_W)

// ---------- Tempest Weather Station Configuration ----------
#define TEMPEST_UDP_PORT            50222
#define DEFAULT_TEMPEST_STATION_ID  0
#define DEFAULT_TEMPEST_API_TOKEN   ""

// REST forecast polling interval (10 minutes)
#define TEMPEST_FORECAST_INTERVAL_MS 600000UL

// Units: 0 = Imperial (°F, mph, inHg, miles), 1 = Metric (°C, m/s, mb, km)
enum UnitSystem {
    UNIT_IMPERIAL = 0,
    UNIT_METRIC   = 1
};

#define DEFAULT_UNIT_SYSTEM         UNIT_IMPERIAL
#define DEFAULT_ROTATION            270   // 270° clockwise = 90° to the left
#define TZ_STR                      "CST6CDT,M3.2.0,M11.1.0" // US Central Time (configurable)
#define BRIGHTNESS_DEFAULT          210   // 0..255
#ifndef BRIGHTNESS_DIM
#  define BRIGHTNESS_DIM            35
#endif
#define IDLE_DIM_MS                 45000 // 45 seconds to dim
