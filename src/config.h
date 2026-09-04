#pragma once
#include <stdint.h>

// Weather Station Display — Hardware & Network Configuration

#define FW_NAME             "Weather Station Display"
#define FW_VERSION          "1.0.2"
#define MDNS_HOSTNAME       "weather"          // http://weather.local/
#define AP_NAME             "Weather-Display-Setup"
#define DEFAULT_WIFI_SSID   ""
#define DEFAULT_WIFI_PASS   ""

// ---------- Screen Geometry ----------
#define SCREEN_W            466
#define SCREEN_H            466
#define SCREEN_CX           233
#define SCREEN_CY           233
#define LCD_COL_OFFSET      6
#define LCD_ROW_OFFSET      0
#define LCD_QSPI_HZ         80000000

// ---------- Display & Touch Pins ----------
#define PIN_LCD_CS          12
#define PIN_LCD_SCLK        38
#define PIN_LCD_D0          4
#define PIN_LCD_D1          5
#define PIN_LCD_D2          6
#define PIN_LCD_D3          7
#define PIN_LCD_RST         39

#define PIN_I2C_SDA         15
#define PIN_I2C_SCL         14
#define PIN_TP_INT          11
#define PIN_TP_RST          40
#define I2C_ADDR_TOUCH      0x5A    // CST9217 capacitive touch

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
#define BRIGHTNESS_DIM              35
#define IDLE_DIM_MS                 45000 // 45 seconds to dim

