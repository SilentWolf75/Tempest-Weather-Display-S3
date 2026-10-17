# Tempest Weather Display S3 & P4

A standalone desk weather console for a WeatherFlow Tempest station, with five swipeable screens and a local settings dashboard.

- **S3:** Waveshare ESP32-S3-Touch-AMOLED-1.75, 466 × 466 round AMOLED.
- **P4:** Waveshare ESP32-P4-WIFI6-Touch-LCD-4C, 720 × 720 round MIPI-DSI LCD with a C6 Wi-Fi coprocessor.
- **Installer:** [Install from Chrome or Edge](https://SilentWolf75.github.io/Tempest-Weather-Display-S3/).
- **License:** [MIT](LICENSE).

## Setup and access

1. Connect the supported board with a USB data cable and use the web installer. It detects the chip family. Select **Erase device** only for a fresh installation; normal updates preserve settings.
2. On first boot, join **Weather-Display-Setup** using the unique **Setup key** shown on the main screen. You can also find it on the Info screen.
3. Open **http://192.168.4.1/**. Sign in as **admin**, using that same key as the password. Select a Wi-Fi network and save its credentials.
4. After connection, open **http://weather.local/** or the address shown on the Info screen. The setup hotspot closes after the station connection succeeds.
5. Enter a Tempest Station ID and personal API token for cloud observations, forecasts, and complete daily rain totals. Local UDP observations work without a token.
6. Optional: enter your sensor's **ST-… serial number** to accept UDP readings only from that sensor. A cloud Station ID does not identify a LAN UDP sensor automatically.

On first boot, if Wi-Fi is still down after 45 seconds, the password-protected setup hotspot opens. After the display has already been online, a dropout uses the chip's auto-reconnect and the hotspot waits three minutes, so a short outage does not interrupt the station. A failed connection does not freeze rendering. Saving a different Wi-Fi network starts a connection attempt after the response has been sent, and that new attempt gets the 45-second hotspot timeout.

The generated password is stored in NVS and survives normal updates. Erasing the device generates a new one. It is displayed on the physical device and is never returned through the status API. Blank Wi-Fi passwords and API tokens preserve their saved values. Use **This network has no password** for an open network, or **Remove saved token** to explicitly clear an API token.

The local dashboard uses HTTP Digest authentication and rejects cross-origin browser requests. Local HTTP payloads are not encrypted; use a trusted local network. WeatherFlow HTTPS validates its certificate using [Amazon Root CA 1](https://www.amazontrust.com/repository/AmazonRootCA1.pem), and waits for a synchronized clock. The firmware must be updated if the service changes its certificate authority.

## Weather screens and data accuracy

- **Weather:** temperature, humidity, dew point, forecast, pressure trend, and observed pressure history. The data source is always shown as Live sensor, Cloud, Stale, or Waiting. Missing measurements show `--`.
- **Wind:** rapid wind uses its own eight-second freshness window, then falls back to the observation average. The peak marker expires after 30 minutes.

- **Rain:** live rain rate, daily accumulation, yesterday's accumulation, and forecast probability. Local totals are labelled **partial** until reconciled with an available complete cloud total. `Rain*` on the main screen also means partial.
- **Lightning:** distance rings show recent events without implying a bearing. The closest distance comes from received strike events, while counts come from deduplicated observation reports. Three-hour counts and closest-distance history use minute buckets and expire with one-minute resolution, including during quiet periods. **Observed today** is the count received since boot or local midnight, not a guaranteed complete station-day total.
- **Info:** connection details, source and reading age, firmware version, and the local login password.

The wind screen and dashboard identify Rapid wind, Sensor average, or Cloud average and show the reading age. Stale wind speed, direction, and observation statistics become unavailable rather than looking current. Fresh rapid wind can still be shown when the slower observation has expired; its gust/lull/average remain unavailable. Calm wind below 0.5 m/s has no compass needle or directional label. Missing individual statistics show `--`, and synchronized timestamps prevent newly received old packets from looking fresh.

The rain screen and dashboard distinguish current rate from daily accumulation. Stale or missing rates show `--`; the device gauge resets and uses a neutral color instead of indicating active rain. Known daily totals remain available when a rate expires and retain their complete/partial designation. A complete cloud total can be displayed even before a valid observation arrives. The source and observation age are shown alongside the rate. A fresh zero rate does not inherit an old active-rain event; hail reported in a fresh observation remains identified. Forecast probability becomes unavailable after the forecast expires, including when its last value was zero.

Lightning counts show `--` until a sensor observation report has been received. The dashboard's **Lightning History** card retains observed counts while identifying stale sensor reception, and shows the closest event distance within three hours plus the age of the last strike event. Last-strike ages remain visible beyond a day; they show **Time unknown** when the clock cannot establish the age, and **Not received** when no strike event has arrived. A just-received event shows zero seconds rather than disappearing. Saved lightning buckets survive a reboot, but they still do not establish an all-clear or a complete storm total.

The weather screen also shows the age of the observation. Cloud reading ages include time before download, so an old observation does not look freshly measured. The dashboard shows forecast age, clock synchronization, uptime, and firmware version. If it loses contact, live readings are cleared and the last received chart is explicitly marked unavailable.

The dashboard's **Environment** card shows UV index, solar radiation in W/m², and the outdoor Tempest sensor's battery voltage. Fresh zero UV/solar readings remain visible; missing, invalid, and stale values show `--`. UV and solar follow the current observation's source and freshness window (90 seconds for sensor readings, 20 minutes for cloud readings). Battery voltage has its own sensor timestamp and 90-second window, so cloud refreshes cannot make it appear current. The card shows separate ages for environment and battery readings; voltage is not converted to an estimated battery percentage.

The Info screen and dashboard identify the restart reason for the current boot, such as power-on, software reset, watchdog, or brownout. This is the SDK's reported reason, not a saved reboot log. The dashboard also reports free internal heap, its minimum since boot, and the largest free internal allocation block; these are separate from PSRAM capacity.

**Download Diagnostic Report** collects a fresh troubleshooting snapshot with firmware/board, restart reason, uptime, memory readings, Wi-Fi connection/signal, clock synchronization, weather freshness, and cloud request state. It uses an explicit field whitelist and excludes Wi-Fi names/IP/MAC addresses, station/sensor IDs, location settings, credentials, and weather-history samples. Missing fields are null. The report's capture timestamp comes from the browser clock and is written in UTC. A failed read produces an error rather than exporting old dashboard values; downloading does not change settings or restart the device.

The dashboard's pressure chart uses measured samples and their timestamps, with units matching your settings. It starts with the history collected since boot and grows to 24 hours; it never fills missing history with invented points. You can select any of the five screens from the dashboard or tap the enlarged page-indicator targets on the device. Screen selection pauses auto-scroll for ten seconds; Info stays selected until you leave it.

**Download Pressure CSV** reads the currently collected history from the device and exports timestamp/pressure rows, using UTC timestamps and explicit `mb` or `inHg` column units. It includes no credentials or network details. A single collected sample can be exported; empty or invalid history and failed device reads produce an error instead of downloading cached or invented data. Chart start/end dates use the browser's timezone, which may differ from the display's configured timezone. Export does not clear history or change settings.

Pressure history starts empty and adds one measured sample every 30 minutes, up to 48 samples. The three-hour trend appears only after three hours of measurements; a gap over one hour restarts history. Pressure samples and the local rain ledger are stored in NVS when a sample is recorded, when the local day rolls over, or when a cloud total is reconciled, so a reboot keeps the chart and yesterday's complete total. Samples from the last half hour before power loss can still be missing. Yesterday's rain remains unavailable unless a complete total is known. Local day boundaries use the selected timezone.

UDP observation and rapid-wind timestamps reject duplicate and out-of-order packets. Fresh UDP observations take precedence over cloud observations. The cloud endpoint is polled every ten minutes for forecasts, and its observations refresh the display whenever UDP observations have been absent for 90 seconds. A Wi-Fi reconnect triggers a refresh without creating duplicate tasks.

After a failed cloud request, transient failures retry after 30 seconds, then 1, 2, 4, 8, and 10 minutes; success resets the retry schedule. Access errors, missing stations, and rate limits wait ten minutes instead of repeatedly sending bad requests. Numeric `Retry-After` headers extend that wait (up to seven days). Service retry windows remain in effect for queued manual refreshes and Wi-Fi reconnects; changing station credentials starts a new schedule. Requests wait for Wi-Fi, a synchronized clock, and a configured station/token.

The dashboard's **Cloud Weather** card shows the current state, last result, last attempt age, and next attempt countdown. Refresh controls indicate queued and active requests, and failure messages never include API tokens. Missing daily high/low values show `--` rather than the current temperature. Responses without a daily forecast entry or condition/icon do not refresh the forecast timestamp. Forecast conditions, high/low/feels-like values, and rain probability are marked unavailable after the forecast expires, while received observation values remain separate.

The protocol is documented in [WeatherFlow's UDP reference](https://weatherflow.github.io/Tempest/api/udp/v171/). `obs_st` lightning distance is an interval average, and `evt_strike` contains distance and energy but no direction. The display does not claim a storm location or an all-clear status.

Missing or invalid UV, solar radiation, and sensor battery values in a UDP observation no longer discard its other valid measurements. These optional values show as unavailable rather than being converted to zero; genuine zero UV and solar readings remain valid. Core measurement fields, timestamps, integer wind directions, precipitation types, and strike counts still require valid numeric types. The physical weather screen's UV footer uses the same freshness checks as the dashboard.

The dashboard's **Current Weather** card groups temperature, relative humidity, calculated dew point, and station pressure with their observation source and age. It follows the same freshness checks as the physical weather screen: stale current values show `--`, while collected pressure history remains available. Dew point is unavailable at zero humidity instead of incorrectly matching air temperature. Temperature and dew point follow your temperature units, and station pressure follows your pressure units.

The dashboard's **Cloud Outlook** card shows cloud conditions, daily high and low, and the cloud's feels-like reading in the selected temperature units. Its age is explicitly time since download, separate from sensor observation age. Both the card and physical weather screen use the same 20-minute forecast expiry window; expired values become unavailable. Missing individual temperatures show `--`, including when other forecast fields remain available. A new forecast without conditions or an icon clears the old text/icon rather than carrying them forward into a fresh download.

The dashboard includes **Check Connection** for an immediate status read and shows when a check is running. Automatic reads run four seconds after the preceding read finishes, without overlapping requests. Hidden tabs pause polling; returning to the tab or regaining browser connectivity immediately checks the device. Pending settings edits stay intact. Failed reads clear current values and retry while the tab is visible.

The dashboard's persistent section links jump to **Weather**, **Display**, **Setup**, **Diagnostics**, and **Updates** without reloading the page or changing settings. They remain accessible while scrolling, wrap into two rows on phones, and support keyboard navigation. Browser Back returns through section jumps while preserving unsaved form edits.

## Display settings

Day and idle brightness, dim timeout, auto-scroll, units, timezone, and night schedule are configurable through the dashboard. The dim-brightness slider saves its value. Live changes show success after the device acknowledges them; invalid values are rejected.

Choose **Use a 24-hour clock** for the device clock and dashboard local time. Normal upgrades retain the 12-hour default unless you enable it. Clock and other display-only saves do not trigger a cloud refresh.

The dashboard marks unsaved edits and warns when you leave with pending changes. Live controls clear their pending state only after an acknowledged save; other edits stay pending. Restore, reboot, and firmware upload require you to save those edits first. Saves time out after eight seconds, retain edits on failure, and prevent duplicate submissions. Edits made while a save is in progress remain in the form and need another save. A failed reboot request is reported as unconfirmed.

**Display Preferences Backup** downloads the device's saved units, clock, timezone, brightness, startup screen, scroll selections, rotation, and night schedule as a small JSON file. It excludes Wi-Fi details, station/sensor identifiers, API tokens, and the device login password. Unsaved form edits are not exported.

Choose a backup file to review the differences, then select **Apply Preferences** to save them. Preview does not write to the device, and network/station settings are preserved. Unsupported rotation and low nonzero brightness are adapted to the target board and shown before applying; zero brightness stays off. Invalid, oversized (over 16 KB), or unsupported backups are rejected. A rejected save keeps the preview available to retry. Applying reloads the settings form, so save other edits first. Startup-screen changes take effect after restart.

Night brightness is applied throughout the scheduled hours, including while the screen is already idle. Equal start/end hours disable the schedule window. A five-second brightness preview expires on the device even if the browser closes. Auto-scroll respects the configured interval after touch and excludes the Info screen.

Choose a **Startup Screen** to open after a restart, and select which weather screens **Auto-Scroll** visits. With one selected screen it stays on that screen; with no selections it holds the current screen. Manual swipes and page indicators still reach all five screens. An unconfigured device opens Weather so setup instructions remain visible.

**Touch Wake During Night Mode** raises brightness to the idle level for 30 seconds after touch by default, including when night brightness is zero. Choose 15 seconds to five minutes, or disable it. A zero idle level uses the default dim level for wake-up; P4 still applies its hardware minimum. It never lowers a brighter configured night level, and returns to the night level when the touch inactivity period expires. Powering on alone does not trigger touch wake.

S3 and P4 both support 0°, 90°, 180°, and 270° rotation. P4 rotation is applied in software because the panel's scanout stays fixed. P4's minimum lit brightness is 64/255; lower nonzero settings use that minimum, while zero switches the backlight off.

## Build, test, and package

Use PlatformIO Core with the versions pinned in `platformio.ini`. The two firmware environments have separate display and touch drivers.

```sh
pio run -e esp32-s3-amoled-175 -e esp32-p4-lcd-4c
pio run -e esp32-s3-amoled-175 -t upload --upload-port COM27
pio device monitor -b 115200
```

The S3 uses 16 MB flash and OPI PSRAM. The P4 board definition describes 32 MB flash/PSRAM; its current partition layout uses the first 16 MB. P4 display timings are in `src/boards/waveshare_p4_lcd_4c.h`.

Run the host regression suite after dependencies have been installed by a build:

```sh
# Linux/macOS: a C++17 compiler and Node.js are required
python tools/run_tests.py

# Windows: Visual Studio C++ build tools and Node.js are required
tools\test_windows.cmd
```

Tests compile the actual weather state, settings, and brightness code with controlled clocks and storage. The actual web handlers are tested with simulated requests and updates; dashboard JavaScript is checked for acknowledgement and rejection behavior. Hardware rendering, radio operation, and real flash interruptions still require device testing.

```sh
# Package both successful builds, including factory images and SHA-256 checksums
python tools/package_release.py

# Also refresh local web installer assets; this does not publish the website
python tools/package_release.py --web
```

The version in `src/config.h` drives release metadata, installer labels, and manifests. Packaging requires both board builds to match it. Flash offsets are taken from PlatformIO rather than shared between boards: notably, P4's bootloader starts at `0x2000`, while S3's starts at `0x0000`. Use the board's **firmware.bin** for authenticated OTA updates; factory images are for USB installation. An OTA upload is rejected unless its image header matches this board's chip and the running firmware's project name, and the image contains this project's marker. The P4 module has 32 MB of flash; the partition table uses the first 16 MB, and weather history stays in NVS rather than a second data partition.

GitHub Actions builds both targets, runs the regression suite, and packages artifacts without publishing or flashing a device.

## Screenshots

Screen images are not stored in this repository. `python tools/grab_screens.py DEVICE_IP` asks for the device password and captures authenticated screenshots. It applies opaque redactions to login/network details before writing public assets. Review screenshots before publishing them.
