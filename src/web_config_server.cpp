#include "web_config_server.h"
#include "settings_mgr.h"
#include "tempest_state.h"
#include "tempest_rest.h"
#include "display.h"
#include "config.h"
#include "ui/ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>

static WebServer s_server(80);
static bool s_server_started = false;

static const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Weather Station Display - Settings</title>
  <style>
    :root {
      --bg: #0b0f19;
      --card: #151d2c;
      --border: #243048;
      --text: #f8fafc;
      --dim: #94a3b8;
      --accent: #38bdf8;
      --accent-hover: #0284c7;
      --ok: #10b981;
      --warn: #f59e0b;
      --alert: #ef4444;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background: var(--bg);
      color: var(--text);
      padding: 16px;
      display: flex;
      justify-content: center;
    }
    .container {
      width: 100%;
      max-width: 680px;
    }
    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding-bottom: 14px;
      margin-bottom: 20px;
      border-bottom: 1px solid var(--border);
    }
    .title {
      font-size: 20px;
      font-weight: 700;
      color: var(--accent);
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .badge {
      padding: 3px 8px;
      border-radius: 9999px;
      font-size: 11px;
      font-weight: 600;
      background: #064e3b;
      color: #34d399;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 18px;
      margin-bottom: 18px;
    }
    .card-title {
      font-size: 15px;
      font-weight: 600;
      color: var(--dim);
      text-transform: uppercase;
      letter-spacing: 0.05em;
      margin-bottom: 14px;
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .form-group {
      margin-bottom: 14px;
    }
    .form-group:last-child {
      margin-bottom: 0;
    }
    label {
      display: block;
      font-size: 13px;
      font-weight: 500;
      color: var(--dim);
      margin-bottom: 6px;
    }
    input[type="text"], input[type="number"], input[type="password"], select {
      width: 100%;
      padding: 10px 12px;
      background: #0b0f19;
      border: 1px solid var(--border);
      border-radius: 8px;
      color: var(--text);
      font-size: 14px;
      outline: none;
      transition: border-color 0.2s;
    }
    input:focus, select:focus {
      border-color: var(--accent);
    }
    .range-wrap {
      display: flex;
      align-items: center;
      gap: 12px;
    }
    input[type="range"] {
      flex: 1;
      accent-color: var(--accent);
    }
    .range-val {
      font-size: 14px;
      font-weight: 600;
      width: 48px;
      text-align: right;
    }
    .radio-group {
      display: flex;
      gap: 12px;
    }
    .radio-label {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      padding: 10px;
      background: #0b0f19;
      border: 1px solid var(--border);
      border-radius: 8px;
      cursor: pointer;
      font-size: 14px;
      font-weight: 600;
      transition: all 0.2s;
    }
    .radio-label input { display: none; }
    .radio-label.active {
      border-color: var(--accent);
      background: rgba(56, 189, 248, 0.12);
      color: var(--accent);
    }
    .btn-row {
      display: flex;
      gap: 12px;
      margin-top: 20px;
    }
    button {
      flex: 1;
      padding: 12px;
      border: none;
      border-radius: 8px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      transition: background-color 0.2s, transform 0.1s;
    }
    button:active { transform: scale(0.98); }
    .btn-primary {
      background: var(--accent);
      color: #0b0f19;
    }
    .btn-primary:hover { background: var(--accent-hover); }
    .btn-secondary {
      background: #1e293b;
      color: var(--dim);
    }
    .btn-secondary:hover { background: #334155; color: var(--text); }
    .btn-warn {
      background: rgba(239, 68, 68, 0.2);
      color: var(--alert);
      border: 1px solid var(--alert);
    }
    .btn-warn:hover { background: var(--alert); color: #fff; }
    .status-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      font-size: 13px;
    }
    .status-item {
      background: #0b0f19;
      padding: 10px;
      border-radius: 8px;
      border: 1px solid var(--border);
    }
    .status-label { color: var(--dim); font-size: 11px; margin-bottom: 2px; }
    .status-val { font-size: 14px; font-weight: 600; color: var(--text); }
    #msg_banner {
      display: none;
      padding: 12px 14px;
      border-radius: 8px;
      margin-bottom: 16px;
      font-size: 14px;
      font-weight: 600;
    }
    .msg-ok { background: #064e3b; color: #34d399; }
    .msg-err { background: #7f1d1d; color: #fecaca; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="title">
        <span>🌤️</span> Weather Station Display
      </div>
      <div class="badge" id="net_status">● Connected</div>
    </div>

    <div id="msg_banner"></div>

    <form id="cfg_form">
      <!-- Wi-Fi Network Configuration -->
      <div class="card">
        <div class="card-title">📶 Wi-Fi Network</div>
        <div class="form-group">
          <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:6px;">
            <label style="margin:0;" for="wifi_ssid">Network Name (SSID)</label>
            <button type="button" id="btn_scan_wifi" class="btn-secondary" style="padding:2px 8px; font-size:11px; height:24px; line-height:1; border-radius:4px;" onclick="scanWifi()">🔍 Scan Networks</button>
          </div>
          <select id="wifi_scan_select" style="display:none; margin-bottom:8px; border-color:var(--accent);" onchange="onWifiSelect(this.value)">
            <option value="">-- Discovered Networks --</option>
          </select>
          <input type="text" id="wifi_ssid" name="wifi_ssid" placeholder="Enter Wi-Fi SSID or select above">
        </div>
        <div class="form-group">
          <label for="wifi_pass">Password (leave blank to keep current)</label>
          <input type="password" id="wifi_pass" name="wifi_pass" placeholder="••••••••">
        </div>
      </div>

      <!-- Time & Location -->
      <div class="card">
        <div class="card-title">🕒 Time & Location</div>
        <div class="form-group">
          <label for="tz_preset">Timezone</label>
          <select id="tz_preset" name="tz_preset" onchange="toggleCustomTz(); liveSetTz(this.value);">
            <option value="0">US Eastern (ET)</option>
            <option value="1">US Central (CT)</option>
            <option value="2">US Mountain (MT)</option>
            <option value="3">US Arizona (MST)</option>
            <option value="4">US Pacific (PT)</option>
            <option value="5">Alaska (AKT)</option>
            <option value="6">Hawaii (HST)</option>
            <option value="7">UTC (Coordinated Universal Time)</option>
            <option value="8">Custom POSIX String</option>
          </select>
        </div>
        <div class="form-group" id="custom_tz_group" style="display:none;">
          <label for="tz_custom">Custom POSIX Timezone</label>
          <input type="text" id="tz_custom" name="tz_custom" placeholder="e.g. CST6CDT,M3.2.0,M11.1.0">
        </div>
        <div class="form-group">
          <label for="zipcode">Zip Code (for local forecast & radar)</label>
          <input type="text" id="zipcode" name="zipcode" maxlength="10" placeholder="e.g. 90210">
        </div>
      </div>

      <!-- Tempest Station Settings -->
      <div class="card">
        <div class="card-title">🛰️ Tempest Weather Station</div>
        <div class="form-group">
          <label for="station_id">Tempest Station ID</label>
          <input type="number" id="station_id" name="station_id" placeholder="e.g. 12345">
        </div>
        <div class="form-group">
          <label for="api_token">Personal Access API Token</label>
          <input type="text" id="api_token" name="api_token" placeholder="Enter Tempest API Token">
        </div>
      </div>

      <!-- Display & Units (LIVE FEEDBACK) -->
      <div class="card">
        <div class="card-title">⚙️ Units & Screen <span style="font-size:11px; color:var(--ok); margin-left:8px; font-weight:normal; background:#064e3b; padding:2px 7px; border-radius:9999px;">⚡ Live Updates Active</span></div>
        <div class="form-group">
          <label>Measurement Units</label>
          <div class="radio-group">
            <label class="radio-label" id="lbl_imperial">
              <input type="radio" name="units" value="0" onchange="updateRadioStyles(); liveSetUnits(this.value);">
              Imperial (°F, mph, inHg)
            </label>
            <label class="radio-label" id="lbl_metric">
              <input type="radio" name="units" value="1" onchange="updateRadioStyles(); liveSetUnits(this.value);">
              Metric (°C, m/s, mb)
            </label>
          </div>
        </div>
        <div class="form-group">
          <label for="screen_rotation">Screen Rotation</label>
          <select id="screen_rotation" name="screen_rotation" onchange="liveSetRotation(this.value)">
            <option value="270">90° Left (270° Clockwise) - Recommended</option>
            <option value="90">90° Right (90° Clockwise)</option>
            <option value="0">Default (0°)</option>
            <option value="180">Inverted (180°)</option>
          </select>
        </div>
        <div class="form-group">
          <label>Day Screen Brightness (<span id="b_day_val">210</span>)</label>
          <div class="range-wrap">
            <input type="range" id="brightness_day" name="brightness_day" min="20" max="255" value="210"
                   oninput="liveBrightness(this.value)" onchange="liveSaveBrightness(this.value)">
          </div>
        </div>
        <div class="form-group">
          <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:6px;">
            <label style="margin:0;">Dim Idle Brightness (<span id="b_dim_val">35</span>)</label>
            <button type="button" id="btn_preview_dim" class="btn-secondary" style="padding:2px 8px; font-size:11px; height:24px; line-height:1; border-radius:4px;" onclick="toggleDimPreview()">👁️ Preview (5s)</button>
          </div>
          <div class="range-wrap">
            <input type="range" id="brightness_dim" name="brightness_dim" min="5" max="100" value="35"
                   oninput="liveDimBrightness(this.value)" onchange="liveSaveDimBrightness(this.value)">
          </div>
        </div>
        <div class="form-group">
          <label for="dim_timeout_s">Display Dim Timeout</label>
          <select id="dim_timeout_s" name="dim_timeout_s" onchange="liveSetDimTimeout(this.value)">
            <option value="0">Never (Stay at Day Brightness)</option>
            <option value="15">15 Seconds</option>
            <option value="30">30 Seconds</option>
            <option value="45">45 Seconds (Default)</option>
            <option value="60">1 Minute</option>
            <option value="120">2 Minutes</option>
            <option value="300">5 Minutes</option>
            <option value="600">10 Minutes</option>
          </select>
        </div>
        <div class="form-group">
          <label for="auto_scroll_s">Auto-Scroll Screens Interval</label>
          <select id="auto_scroll_s" name="auto_scroll_s" onchange="liveSetAutoScroll(this.value)">
            <option value="0">Disabled (Manual Swipe Only)</option>
            <option value="5">5 Seconds</option>
            <option value="10">10 Seconds (Default)</option>
            <option value="15">15 Seconds</option>
            <option value="20">20 Seconds</option>
            <option value="30">30 Seconds</option>
            <option value="60">1 Minute</option>
          </select>
        </div>
      </div>

      <!-- Buttons -->
      <div class="btn-row">
        <button type="submit" class="btn-primary">💾 Save Settings</button>
        <button type="button" class="btn-secondary" onclick="refreshForecast()">🔄 Refresh Forecast</button>
        <button type="button" class="btn-warn" onclick="restartDevice()">⚠️ Reboot</button>
      </div>
    </form>

    <!-- Diagnostics -->
    <div class="card" style="margin-top: 20px;">
      <div class="card-title">📊 Live Status & Diagnostics</div>
      <div class="status-grid">
        <div class="status-item"><div class="status-label">Device IP</div><div class="status-val" id="st_ip">--</div></div>
        <div class="status-item"><div class="status-label">Local Time</div><div class="status-val" id="st_time">--</div></div>
        <div class="status-item"><div class="status-label">Wi-Fi Signal</div><div class="status-val" id="st_rssi">-- dBm</div></div>
        <div class="status-item"><div class="status-label">UDP Broadcasts</div><div class="status-val" id="st_udp">--</div></div>
        <div class="status-item"><div class="status-label">Live Temperature</div><div class="status-val" id="st_temp">--</div></div>
        <div class="status-item"><div class="status-label">Live Pressure</div><div class="status-val" id="st_press">--</div></div>
      </div>
    </div>
  </div>

  <script>
    let liveTimer = null;
    function sendLive(payload) {
      fetch('/api/live', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).catch(e => console.error("Live update error", e));
    }

    function liveBrightness(val) {
      document.getElementById('b_day_val').innerText = val;
      if (liveTimer) clearTimeout(liveTimer);
      liveTimer = setTimeout(() => {
        sendLive({ brightness_day: parseInt(val), save: false });
      }, 25);
    }

    function liveSaveBrightness(val) {
      if (liveTimer) clearTimeout(liveTimer);
      sendLive({ brightness_day: parseInt(val), save: true });
      showBanner("Day brightness updated live (" + val + ")", true);
    }

    let isDimPreviewing = false;
    let dimPreviewTimeout = null;

    function liveDimBrightness(val) {
      document.getElementById('b_dim_val').innerText = val;
      if (isDimPreviewing) {
        sendLive({ preview_dim: parseInt(val), save: false });
      }
    }

    function liveSaveDimBrightness(val) {
      sendLive({ brightness_dim: parseInt(val), save: true });
      showBanner("Dim brightness saved (" + val + ")", true);
    }

    function toggleDimPreview() {
      let dayVal = parseInt(document.getElementById('brightness_day').value);
      let dimVal = parseInt(document.getElementById('brightness_dim').value);
      let btn = document.getElementById('btn_preview_dim');

      if (!isDimPreviewing) {
        isDimPreviewing = true;
        btn.innerText = "⏹️ Exit";
        btn.style.background = "#d97706";
        btn.style.color = "#ffffff";
        sendLive({ preview_dim: dimVal, save: false });
        showBanner("Previewing Dim level (" + dimVal + ")...", true);

        if (dimPreviewTimeout) clearTimeout(dimPreviewTimeout);
        dimPreviewTimeout = setTimeout(() => {
          if (isDimPreviewing) toggleDimPreview();
        }, 5000);
      } else {
        isDimPreviewing = false;
        btn.innerText = "👁️ Preview (5s)";
        btn.style.background = "";
        btn.style.color = "";
        if (dimPreviewTimeout) clearTimeout(dimPreviewTimeout);
        sendLive({ brightness_day: dayVal, save: false });
        showBanner("Restored Day brightness (" + dayVal + ")", true);
      }
    }

    function liveSetRotation(val) {
      sendLive({ screen_rotation: parseInt(val), save: true });
      showBanner("Screen rotated live to " + val + "°", true);
    }

    function liveSetUnits(val) {
      sendLive({ units: parseInt(val), save: true });
      showBanner("Units switched to " + (val === '0' ? 'Imperial' : 'Metric'), true);
    }

    function liveSetDimTimeout(val) {
      sendLive({ dim_timeout_s: parseInt(val), save: true });
      showBanner("Dim timeout set to " + (val === '0' ? 'Never' : val + 's'), true);
    }

    function liveSetAutoScroll(val) {
      sendLive({ auto_scroll_s: parseInt(val), save: true });
      showBanner("Auto-scroll set to " + (val === '0' ? 'Disabled' : val + 's'), true);
    }

    function liveSetTz(val) {
      sendLive({ tz_preset: parseInt(val), save: true });
      showBanner("Timezone updated live!", true);
    }

    async function scanWifi() {
      let btn = document.getElementById('btn_scan_wifi');
      let sel = document.getElementById('wifi_scan_select');
      btn.innerText = "⏳ Scanning...";
      btn.disabled = true;

      try {
        let res = await fetch('/api/wifi_scan');
        let data = await res.json();
        sel.innerHTML = '<option value="">-- Discovered Networks (' + (data.networks ? data.networks.length : 0) + ') --</option>';
        if (data.networks && data.networks.length > 0) {
          data.networks.sort((a, b) => b.rssi - a.rssi);
          data.networks.forEach(n => {
            let opt = document.createElement('option');
            opt.value = n.ssid;
            let lock = n.secure ? "🔒 " : "🌐 ";
            let signal = (n.rssi > -60) ? "Strong" : (n.rssi > -75) ? "Fair" : "Weak";
            opt.innerText = lock + n.ssid + " (" + signal + " " + n.rssi + " dBm)";
            sel.appendChild(opt);
          });
          sel.style.display = "block";
          showBanner("Found " + data.networks.length + " Wi-Fi networks.", true);
        } else {
          showBanner("No networks found. Try scanning again.", false);
        }
      } catch (err) {
        showBanner("Wi-Fi scan failed: " + err.message, false);
      } finally {
        btn.innerText = "🔍 Scan Networks";
        btn.disabled = false;
      }
    }

    function onWifiSelect(val) {
      if (val) {
        document.getElementById('wifi_ssid').value = val;
        document.getElementById('wifi_pass').focus();
      }
    }

    function toggleCustomTz() {
      let v = document.getElementById('tz_preset').value;
      document.getElementById('custom_tz_group').style.display = (v === '8') ? 'block' : 'none';
    }

    function updateRadioStyles() {
      let isMetric = document.querySelector('input[name="units"]:checked').value === '1';
      document.getElementById('lbl_imperial').classList.toggle('active', !isMetric);
      document.getElementById('lbl_metric').classList.toggle('active', isMetric);
    }

    function showBanner(msg, isOk) {
      let b = document.getElementById('msg_banner');
      b.className = isOk ? 'msg-ok' : 'msg-err';
      b.innerText = msg;
      b.style.display = 'block';
      setTimeout(() => { b.style.display = 'none'; }, 4000);
    }

    let formInitialized = false;

    async function loadTelemetry() {
      try {
        let res = await fetch('/api/status');
        if (!res.ok) throw new Error("API error");
        let data = await res.json();

        // Populate form inputs only on initial load
        if (!formInitialized) {
          document.getElementById('wifi_ssid').value = data.settings.wifi_ssid || '';
          document.getElementById('wifi_pass').value = ''; // leave blank for security
          document.getElementById('tz_preset').value = data.settings.tz_preset;
          document.getElementById('tz_custom').value = data.settings.tz_custom || '';
          document.getElementById('zipcode').value = data.settings.zipcode || '';
          document.getElementById('station_id').value = (data.settings.station_id > 0) ? data.settings.station_id : '';
          document.getElementById('api_token').value = data.settings.api_token || '';

          let unitRadios = document.querySelectorAll('input[name="units"]');
          unitRadios.forEach(r => { r.checked = (parseInt(r.value) === data.settings.units); });

          document.getElementById('screen_rotation').value = data.settings.screen_rotation;
          document.getElementById('brightness_day').value = data.settings.brightness_day;
          document.getElementById('b_day_val').innerText = data.settings.brightness_day;
          document.getElementById('brightness_dim').value = data.settings.brightness_dim;
          document.getElementById('b_dim_val').innerText = data.settings.brightness_dim;
          document.getElementById('dim_timeout_s').value = (data.settings.dim_timeout_s !== undefined) ? data.settings.dim_timeout_s : 45;
          document.getElementById('auto_scroll_s').value = (data.settings.auto_scroll_s !== undefined) ? data.settings.auto_scroll_s : 10;

          toggleCustomTz();
          updateRadioStyles();
          formInitialized = true;
        }

        // Live telemetry updates continuously without touching form inputs
        document.getElementById('st_ip').innerText = data.telemetry.ip;
        document.getElementById('st_time').innerText = data.telemetry.local_time;
        document.getElementById('st_rssi').innerText = data.telemetry.rssi + ' dBm';
        document.getElementById('st_udp').innerText = data.telemetry.udp_received ? '● Active' : 'Waiting...';
        document.getElementById('st_temp').innerText = data.telemetry.temp_display;
        document.getElementById('st_press').innerText = data.telemetry.press_display;
      } catch (e) {
        console.error(e);
      }
    }

    document.getElementById('cfg_form').addEventListener('submit', async (e) => {
      e.preventDefault();
      let payload = {
        wifi_ssid: document.getElementById('wifi_ssid').value,
        wifi_pass: document.getElementById('wifi_pass').value,
        tz_preset: parseInt(document.getElementById('tz_preset').value),
        tz_custom: document.getElementById('tz_custom').value,
        zipcode: document.getElementById('zipcode').value,
        station_id: parseInt(document.getElementById('station_id').value),
        api_token: document.getElementById('api_token').value,
        units: parseInt(document.querySelector('input[name="units"]:checked').value),
        screen_rotation: parseInt(document.getElementById('screen_rotation').value),
        brightness_day: parseInt(document.getElementById('brightness_day').value),
        brightness_dim: parseInt(document.getElementById('brightness_dim').value),
        dim_timeout_s: parseInt(document.getElementById('dim_timeout_s').value),
        auto_scroll_s: parseInt(document.getElementById('auto_scroll_s').value)
      };

      try {
        let res = await fetch('/api/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
        let resJson = await res.json();
        if (resJson.status === 'ok') {
          showBanner("Settings saved and applied successfully!", true);
          formInitialized = false; // re-sync on next fetch
          loadTelemetry();
        } else {
          showBanner("Failed to save: " + (resJson.message || "Error"), false);
        }
      } catch (err) {
        showBanner("Error communicating with device", false);
      }
    });

    async function refreshForecast() {
      try {
        let res = await fetch('/api/forecast_refresh', { method: 'POST' });
        showBanner("Forecast refresh triggered!", true);
      } catch (e) {
        showBanner("Failed to trigger refresh", false);
      }
    }

    async function restartDevice() {
      if (!confirm("Are you sure you want to reboot the display?")) return;
      try {
        await fetch('/api/restart', { method: 'POST' });
        showBanner("Device is restarting... Please wait 10 seconds.", true);
        setTimeout(() => { location.reload(); }, 9000);
      } catch (e) {
        showBanner("Reboot triggered.", true);
      }
    }

    loadTelemetry();
    setInterval(loadTelemetry, 4000);
  </script>
</body>
</html>
)rawliteral";

static void handle_root() {
    s_server.send(200, "text/html", HTML_DASHBOARD);
}

static void handle_api_status() {
    AppSettings s;
    settings_get(&s);

    TempestState wx;
    tempest_get_state(&wx);

    time_t now = time(nullptr);
    char time_str[32] = "--:--";
    if (now >= 1000000000LL) {
        struct tm ti;
        localtime_r(&now, &ti);
        strftime(time_str, sizeof(time_str), "%I:%M:%S %p", &ti);
    }

    JsonDocument doc;
    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["wifi_ssid"] = s.wifi_ssid[0] ? s.wifi_ssid : WiFi.SSID();
    settings["tz_preset"] = s.tz_preset;
    settings["tz_name"] = settings_get_tz_name(s.tz_preset);
    settings["tz_custom"] = s.tz_custom;
    settings["zipcode"] = s.zipcode;
    settings["station_id"] = s.station_id;
    settings["api_token"] = s.api_token;
    settings["units"] = s.units;
    settings["screen_rotation"] = s.screen_rotation;
    settings["brightness_day"] = s.brightness_day;
    settings["brightness_dim"] = s.brightness_dim;
    settings["dim_timeout_s"] = s.dim_timeout_s;
    settings["auto_scroll_s"] = s.auto_scroll_s;

    JsonObject telemetry = doc["telemetry"].to<JsonObject>();
    telemetry["ip"] = WiFi.localIP().toString();
    telemetry["rssi"] = WiFi.RSSI();
    telemetry["local_time"] = time_str;
    telemetry["udp_received"] = wx.udp_connected;

    float t_disp = temp_to_unit(wx.air_temp_c, (UnitSystem)s.units);
    char t_buf[24];
    snprintf(t_buf, sizeof(t_buf), "%.1f %s", t_disp, temp_unit_str((UnitSystem)s.units));
    telemetry["temp_display"] = t_buf;

    float p_disp = pressure_to_unit(wx.pressure_mb, (UnitSystem)s.units);
    char p_buf[24];
    snprintf(p_buf, sizeof(p_buf), (s.units == 0 ? "%.2f inHg" : "%.1f mb"), p_disp);
    telemetry["press_display"] = p_buf;

    telemetry["conditions"] = wx.conditions_text;
    telemetry["free_heap"] = ESP.getFreeHeap();
    telemetry["free_psram"] = ESP.getFreePsram();

    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
}

static void handle_api_settings() {
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing payload\"}");
        return;
    }

    String body = s_server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        s_server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parse error\"}");
        return;
    }

    AppSettings s;
    settings_get(&s);

    if (doc["wifi_ssid"].is<const char*>()) {
        const char *new_ssid = doc["wifi_ssid"].as<const char*>();
        if (strlen(new_ssid) > 0) {
            strncpy(s.wifi_ssid, new_ssid, sizeof(s.wifi_ssid) - 1);
            s.wifi_ssid[sizeof(s.wifi_ssid) - 1] = '\0';
        }
    }
    if (doc["wifi_pass"].is<const char*>()) {
        const char *new_pass = doc["wifi_pass"].as<const char*>();
        if (strlen(new_pass) > 0) {
            strncpy(s.wifi_password, new_pass, sizeof(s.wifi_password) - 1);
            s.wifi_password[sizeof(s.wifi_password) - 1] = '\0';
        }
    }

    if (doc["tz_preset"].is<uint8_t>()) s.tz_preset = doc["tz_preset"].as<uint8_t>();
    if (doc["tz_custom"].is<const char*>()) {
        strncpy(s.tz_custom, doc["tz_custom"].as<const char*>(), sizeof(s.tz_custom) - 1);
        s.tz_custom[sizeof(s.tz_custom) - 1] = '\0';
    }
    if (doc["zipcode"].is<const char*>()) {
        strncpy(s.zipcode, doc["zipcode"].as<const char*>(), sizeof(s.zipcode) - 1);
        s.zipcode[sizeof(s.zipcode) - 1] = '\0';
    }
    if (doc["station_id"].is<uint32_t>()) s.station_id = doc["station_id"].as<uint32_t>();
    if (doc["api_token"].is<const char*>()) {
        strncpy(s.api_token, doc["api_token"].as<const char*>(), sizeof(s.api_token) - 1);
        s.api_token[sizeof(s.api_token) - 1] = '\0';
    }
    if (doc["units"].is<uint8_t>()) s.units = doc["units"].as<uint8_t>();
    if (doc["screen_rotation"].is<uint16_t>()) {
        uint16_t new_rot = doc["screen_rotation"].as<uint16_t>();
        if (new_rot != s.screen_rotation) {
            s.screen_rotation = new_rot;
            display::setRotation(new_rot);
        }
    }
    if (doc["brightness_day"].is<uint8_t>()) {
        s.brightness_day = doc["brightness_day"].as<uint8_t>();
        display::setBrightness(s.brightness_day);
    }
    if (doc["brightness_dim"].is<uint8_t>()) s.brightness_dim = doc["brightness_dim"].as<uint8_t>();
    if (doc["dim_timeout_s"].is<uint16_t>()) s.dim_timeout_s = doc["dim_timeout_s"].as<uint16_t>();
    if (doc["auto_scroll_s"].is<uint16_t>()) s.auto_scroll_s = doc["auto_scroll_s"].as<uint16_t>();

    // Save to NVS
    settings_save(&s);

    // Trigger forecast fetch if station ID or API token changed
    tempest_rest_trigger_now();

    s_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_api_live() {
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"error\":\"missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, s_server.arg("plain"));
    if (err) {
        s_server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    AppSettings s;
    settings_get(&s);
    bool need_save = doc["save"].is<bool>() && doc["save"].as<bool>();

    // Live Brightness (Day)
    if (doc["brightness_day"].is<uint8_t>()) {
        uint8_t b = doc["brightness_day"].as<uint8_t>();
        s.brightness_day = b;
        display::setBrightness(b);
    }

    // Live Preview Dim (temporary brightness preview)
    if (doc["preview_dim"].is<uint8_t>()) {
        uint8_t b = doc["preview_dim"].as<uint8_t>();
        display::setBrightness(b);
    }

    // Live Screen Rotation
    if (doc["screen_rotation"].is<uint16_t>()) {
        uint16_t new_rot = doc["screen_rotation"].as<uint16_t>();
        if (new_rot != s.screen_rotation) {
            s.screen_rotation = new_rot;
            display::setRotation(new_rot);
        }
    }

    // Live Units
    if (doc["units"].is<uint8_t>()) {
        s.units = doc["units"].as<uint8_t>();
        tempest_set_units((UnitSystem)s.units);
    }

    // Live Auto-Scroll
    if (doc["auto_scroll_s"].is<uint16_t>()) {
        s.auto_scroll_s = doc["auto_scroll_s"].as<uint16_t>();
    }

    // Live Dim Timeout
    if (doc["dim_timeout_s"].is<uint16_t>()) {
        s.dim_timeout_s = doc["dim_timeout_s"].as<uint16_t>();
    }

    // Live Timezone
    if (doc["tz_preset"].is<uint8_t>()) {
        s.tz_preset = doc["tz_preset"].as<uint8_t>();
        need_save = true;
    }

    if (need_save) {
        settings_save(&s);
    }

    s_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_api_wifi_scan() {
    Serial.println("[web] Scanning available Wi-Fi networks...");
    int16_t n = WiFi.scanNetworks(false, false);
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();

    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue; // skip hidden

            // Deduplicate SSIDs, keeping highest RSSI
            bool found = false;
            for (JsonObject item : arr) {
                if (ssid.equals(item["ssid"].as<const char*>())) {
                    found = true;
                    if (WiFi.RSSI(i) > item["rssi"].as<int>()) {
                        item["rssi"] = WiFi.RSSI(i);
                    }
                    break;
                }
            }
            if (!found) {
                JsonObject item = arr.add<JsonObject>();
                item["ssid"] = ssid;
                item["rssi"] = WiFi.RSSI(i);
                item["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
        }
        WiFi.scanDelete();
    }

    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
}

static void handle_api_forecast_refresh() {
    tempest_rest_trigger_now();
    s_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handle_view() {
    if (s_server.hasArg("i")) {
        int idx = s_server.arg("i").toInt();
        ui_set_screen(idx);
        s_server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        s_server.send(400, "text/plain", "Missing i parameter");
    }
}

static void handle_shot_bmp() {
    const uint16_t *fb = display::captureFrame();
    if (!fb) {
        s_server.send(503, "text/plain", "Capture buffer unavailable");
        return;
    }

    WiFiClient client = s_server.client();
    const uint32_t w = SCREEN_W;
    const uint32_t h = SCREEN_H;
    const uint32_t row_bytes = w * 3;
    const uint32_t pad = (4 - (row_bytes % 4)) % 4;
    const uint32_t image_size = (row_bytes + pad) * h;
    const uint32_t file_size = 54 + image_size;

    uint8_t hdr[54];
    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size & 0xFF; hdr[3] = (file_size >> 8) & 0xFF; hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[26] = 1;
    hdr[28] = 24;
    hdr[34] = image_size & 0xFF; hdr[35] = (image_size >> 8) & 0xFF; hdr[36] = (image_size >> 16) & 0xFF; hdr[37] = (image_size >> 24) & 0xFF;

    s_server.setContentLength(file_size);
    s_server.send(200, "image/bmp", "");

    client.write(hdr, 54);

    uint8_t row_buf[SCREEN_W * 3 + 4];
    memset(row_buf, 0, sizeof(row_buf));

    for (int y = (int)h - 1; y >= 0; --y) {
        const uint16_t *src_row = &fb[y * w];
        uint8_t *dst = row_buf;
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t c = src_row[x];
            uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = ((c >> 5)  & 0x3F) * 255 / 63;
            uint8_t b = (c         & 0x1F) * 255 / 31;
            *dst++ = b;
            *dst++ = g;
            *dst++ = r;
        }
        client.write(row_buf, row_bytes + pad);
    }
}

static void handle_api_restart() {
    s_server.send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

void web_config_server_begin() {
    if (s_server_started) return;

    // Start mDNS service responder: http://weather.local
    MDNS.end();
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.setInstanceName("Weather Station Display");
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "path", "/");
        Serial.printf("[web] mDNS responder started: http://%s.local\n", MDNS_HOSTNAME);
    }

    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/api/status", HTTP_GET, handle_api_status);
    s_server.on("/api/settings", HTTP_POST, handle_api_settings);
    s_server.on("/api/live", HTTP_POST, handle_api_live);
    s_server.on("/api/wifi_scan", HTTP_GET, handle_api_wifi_scan);
    s_server.on("/api/forecast_refresh", HTTP_POST, handle_api_forecast_refresh);
    s_server.on("/api/restart", HTTP_POST, handle_api_restart);
    s_server.on("/shot.bmp", HTTP_GET, handle_shot_bmp);
    s_server.on("/view", HTTP_GET, handle_view);

    s_server.enableCORS(true);
    s_server.begin();
    s_server_started = true;

    Serial.printf("[web] Web configuration server active on port 80 (http://%s/)\n",
                  WiFi.localIP().toString().c_str());
}

void web_config_server_loop() {
    if (s_server_started) {
        s_server.handleClient();
    }
}
