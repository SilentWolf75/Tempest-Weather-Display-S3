# Tempest Weather Display S3

[![Web Installer](https://img.shields.io/badge/Web%20Flasher-Ready-22c55e?style=for-the-badge&logo=espressif)](https://SilentWolf75.github.io/Tempest-Weather-Display-S3/)
[![Firmware](https://img.shields.io/badge/Firmware-v1.0.0-38bdf8?style=for-the-badge)](https://github.com/SilentWolf75/Tempest-Weather-Display-S3/releases)
[![Hardware](https://img.shields.io/badge/Hardware-Waveshare%20ESP32--S3%201.75%22%20AMOLED-orange?style=for-the-badge)](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)

A standalone, high-performance desk weather console for the **[WeatherFlow Tempest](https://weatherflow.com/tempest-weather-system/)** weather system, custom-crafted for the circular **Waveshare ESP32-S3-Touch-AMOLED-1.75** (466×466 round AMOLED + capacitive touch).

⚡ **Zero-Cloud LAN Ingest**: Listens directly to live Tempest UDP broadcast packets on port `50222` for real-time (3-second) rapid wind, barometric pressure, rain rate, and lightning strikes. Automatically falls back to WeatherFlow REST API when outside LAN broadcast range.

🌐 **Flash from your browser**: Install with 1 click directly from Chrome or Edge at **[SilentWolf75.github.io/Tempest-Weather-Display-S3](https://SilentWolf75.github.io/Tempest-Weather-Display-S3/)**.

---

## ✨ Features

- **Circular AMOLED UI (LVGL 8.4)**:
  - **Screen 1 (Main Conditions)**: High-resolution temperature, feels-like, humidity, barometric pressure with 3-hour trend arrows, and procedural animated weather graphics.
  - **Screen 2 (Wind Gauge)**: 360° compass rose needle, live rapid wind speed, gust indicators, and cardinal bearing.
  - **Screen 3 (Lightning Radar)**: Distance ring radar showing closest strike distance (km/mi), strike frequency, and active alerts.
  - **Screen 4 (System Info & Diagnostics)**: Live Wi-Fi SSID, RSSI signal meter, local IP address, mDNS web URL, Tempest station status, MAC address, uptime counter, and firmware version.
- **3D Procedural Weather Vector Graphics**: Volumetric clouds with specular highlights, multi-layer sun corona, dynamic rain streaks with splash ripples, and internal storm strobe flashes.
- **Live Interactive Web Dashboard (`http://weather.local/`)**:
  - **Wi-Fi Scanner**: One-click scan of available 2.4 GHz Wi-Fi networks with signal strength indicators (`📶 Strong / Fair / Weak`) and security lock badges.
  - **Instant Live Hardware Sliders**: Adjust Day Screen Brightness with zero flash wear.
  - **Dim Idle Brightness with 5s Preview**: Evaluate nighttime dim levels in your room lighting without waiting for the timer.
  - **Live Orientation & Units**: Flip screen orientation (`90° Left`, `90° Right`, `0° Default`, `180° Inverted`) and toggle between Imperial (°F, mph, inHg) and Metric (°C, m/s, mb) instantly on the fly.
  - **Configurable Display Dim Timeout & Screen Auto-Scroll**: Cycle across all 4 screens automatically at 5s, 10s, 15s, or 30s intervals.
- **Out-of-the-Box SoftAP Captive Portal**: If unconfigured or moved to a new location, boots a setup hotspot (`Weather-Display-Setup`) at `http://192.168.4.1/`.

---

## 🚀 1-Click Web Flasher (No Toolchain Needed)

1. Open **[SilentWolf75.github.io/Tempest-Weather-Display-S3](https://SilentWolf75.github.io/Tempest-Weather-Display-S3/)** in Google Chrome or Microsoft Edge.
2. Connect the Waveshare ESP32-S3 AMOLED board to your PC via a USB-C data cable.
3. Click **Install Firmware** and select the USB Serial port.
4. For initial installation, check **Erase device**.
5. Once complete, join the board's Wi-Fi hotspot **`Weather-Display-Setup`** from your phone or PC and open `http://192.168.4.1/` to enter your Wi-Fi and Tempest Station ID.

---

## 🛠️ Building with PlatformIO

### Prerequisites
- [PlatformIO Core](https://platformio.org/) or VS Code PlatformIO extension.
- Waveshare ESP32-S3-Touch-AMOLED-1.75.

### Build and Upload
```bash
# Clone the repository
git clone https://github.com/SilentWolf75/Tempest-Weather-Display-S3.git
cd Tempest-Weather-Display-S3

# Compile firmware
pio run

# Upload to board via USB
pio run -t upload --upload-port COM27

# Monitor serial output
pio device monitor -b 115200
```

---

## 🔌 Hardware Specifications & Pinout

| Component | Specification / Pin |
|---|---|
| **MCU** | ESP32-S3R8 (Dual-core Xtensa LX7 @ 240MHz, 8MB PSRAM, 16MB Flash) |
| **Display Controller** | CO5300 QSPI AMOLED (466×466 circular resolution) |
| **Display Pins** | `CS: 12`, `SCLK: 38`, `D0: 4`, `D1: 5`, `D2: 6`, `D3: 7`, `RST: 39` |
| **Touch Controller** | CST9217 Capacitive Touch (`I2C Addr: 0x5A`) |
| **Touch Pins** | `SDA: 15`, `SCL: 14`, `INT: 11`, `RST: 40` |
| **Baud Rate** | Serial Monitor: 115200 · Upload: 921600 |

---

## 📡 Network & Tempest Architecture

```
WeatherFlow Tempest Hub (UDP 50222) ──┐
                                     ├──> [ESP32-S3 FreeRTOS Core 0] ──> [State Mutex]
WeatherFlow REST API (HTTPS Cloud)  ──┘                                       │
                                                                              ▼
Web Config Server (http://weather.local/) <── [LVGL 8.4 UI Core 1] <── [Render Loop]
```

- **Rapid Wind**: Broadcasted over UDP every 3 seconds (`rapid_wind`).
- **Station Observations**: Broadcasted over UDP every 60 seconds (`obs_st`).
- **REST Failover**: Polls WeatherFlow better_forecast endpoint every 10 minutes when UDP broadcast is silent or offline.

---

## 📄 License

This project is open-source software licensed under the **[MIT License](LICENSE)**.
Weather data protocols courtesy of [WeatherFlow Tempest](https://weatherflow.github.io/Tempest/api/).
