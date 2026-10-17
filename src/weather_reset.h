#pragma once
#include <esp_system.h>

inline const char *weather_reset_label(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "Power on";
        case ESP_RST_EXT: return "External reset";
        case ESP_RST_SW: return "Software reset";
        case ESP_RST_PANIC: return "Crash / panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO reset";
        case ESP_RST_USB: return "USB reset";
        case ESP_RST_JTAG: return "JTAG reset";
        case ESP_RST_EFUSE: return "eFuse error";
        case ESP_RST_PWR_GLITCH: return "Power glitch";
        case ESP_RST_CPU_LOCKUP: return "CPU lockup";
        default: return "Unknown";
    }
}
