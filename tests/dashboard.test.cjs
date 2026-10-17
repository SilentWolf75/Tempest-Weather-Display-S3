const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('src/dashboard.html', 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const banners = [];
const requests = [];
const windowListeners = {};
const documentListeners = {};
const scheduledTimers = new Map();
let timerId = 0;
const downloadedBlobs = [];
const downloadLinks = [];
let response = async () => ({ok: true, status: 200, json: async () => ({status: 'ok'})});
const element = id => {
  if (!elements.has(id)) elements.set(id, {value: '0', style: {}, parentElement: {style: {}},
    children: [], files: [], replaceChildren(...children) {this.children = children;},
    attributes: {}, setAttribute(key, value) { this.attributes[key] = value; },
    listeners: {}, classList: {toggle() {}}, addEventListener(name, callback) {this.listeners[name] = callback;}, appendChild(child) {this.children.push(child);}, focus() {}});
  return elements.get(id);
};
Object.defineProperty(element('msg_banner'), 'innerText', {set(text) {banners.push(text);}});
const context = vm.createContext({console: {error() {}},
  setTimeout(callback, delay) {const id = ++timerId; scheduledTimers.set(id, {callback, delay}); return id;},
  clearTimeout(id) {scheduledTimers.delete(id);}, setInterval() {},
  AbortController,
  Blob,
  URL: {createObjectURL(blob) {downloadedBlobs.push(blob); return 'blob:test';}, revokeObjectURL() {}},
  window: {addEventListener(name, callback) {windowListeners[name] = callback;}},
  document: {hidden: false, addEventListener(name, callback) {documentListeners[name] = callback;},
    getElementById: element, querySelectorAll: () => [], querySelector: () => ({value: '0'}),
    body: {appendChild() {}}, createElement: () => ({click() {downloadLinks.push(this.download);}, remove() {}})},
  fetch: async (url, options) => {requests.push({url, payload: options?.body && JSON.parse(options.body)}); return response();},
  confirm: () => false, location: {reload() {}}, FormData: class {append() {}}});
vm.runInContext(script, context);
const flush = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  await flush(); // discard initial telemetry failure in this isolated dashboard test
  banners.length = 0;
  let release;
  response = () => new Promise(resolve => {release = resolve;});
  context.liveSaveDimBrightness('72');
  assert.equal(requests.at(-1).payload.brightness_dim, 72);
  assert.equal(banners.length, 0, 'save must wait for device acknowledgement');
  release({ok: true, status: 200, json: async () => ({status: 'ok'})});
  await flush();
  assert.match(banners.at(-1), /Dim brightness saved/);
  response = async () => ({ok: false, status: 400, json: async () => ({status: 'error', message: 'Invalid setting value'})});
  context.liveSetRotation('90');
  await flush();
  assert.match(banners.at(-1), /Update failed: Invalid setting value/);
  const before = requests.length;
  context.liveSetTz('8');
  assert.equal(requests.length, before, 'custom timezone waits for the full form save');
  assert.equal(context.ageText(null), 'Waiting');
  assert.equal(context.ageText(0), '0s ago');
  assert.equal(context.ageText(3660), '1h 1m ago');
  const diagnosticInput = {settings: {wifi_ssid: 'private-network', wifi_password: 'private-password',
      station_id: 456789, device_serial: 'private-sensor', api_token: 'private-token', admin_password: 'private-admin', zipcode: 'private-location'},
    telemetry: {firmware_version: '1.0.15', chip: 'ESP32-P4', board: 'Demo board', reset_reason: 'Software reset',
      ip: '192.0.2.123', mac: 'private-mac', local_time: 'private-timezone', uptime_s: 0, heap_free: 1024,
      heap_min_free: 0, heap_largest_block: 512, wifi_connected: false, rssi: -60, clock_synced: false,
      feed: 'Waiting for weather', observation_age_s: null, forecast_fresh: false,
      cloud: {configured: false, http_code: 0, result: 'None', api_token: 'private-cloud-token', url: 'private-cloud-url'}}};
  const report = context.makeDiagnosticReport(diagnosticInput);
  const reportText = JSON.stringify(report);
  for (const secret of ['private-', '192.0.2.123', '456789', 'wifi_ssid', 'device_serial', 'zipcode', 'admin_password', 'api_token'])
    assert.ok(!reportText.includes(secret), 'diagnostic report excludes identifiers and credentials');
  assert.equal(report.type, 'weather-display-diagnostics');
  assert.equal(report.format_version, 1);
  assert.match(report.captured_utc, /Z$/);
  assert.equal(report.diagnostics.wifi_connected, false);
  assert.equal(report.diagnostics.heap_min_free, 0);
  assert.equal(report.diagnostics.observation_age_s, null);
  assert.equal(report.diagnostics.cloud.http_code, 0);
  assert.equal(context.makeDiagnosticReport({telemetry: {firmware_version: 'x', heap_free: 'bad'}}).diagnostics.heap_free, null);
  assert.throws(() => context.makeDiagnosticReport({}), /Invalid/);
  context.renderSystem(diagnosticInput.telemetry);
  assert.equal(element('st_heap').innerText, '1.0 KiB');
  assert.equal(element('st_heap_min').innerText, '0.0 KiB');
  assert.equal(element('st_heap_block').innerText, '0.5 KiB');
  assert.equal(element('st_reset').innerText, 'Software reset');
  response = () => new Promise(resolve => {release = resolve;});
  const exportingDiagnostics = context.exportDiagnostics();
  const diagnosticRequestCount = requests.length;
  await context.exportDiagnostics();
  assert.equal(requests.length, diagnosticRequestCount);
  assert.equal(element('diagnostics_export').disabled, true);
  release({ok: true, json: async () => diagnosticInput});
  await exportingDiagnostics;
  assert.equal(downloadLinks.at(-1), 'weather-display-diagnostics.json');
  assert.equal(JSON.parse(await downloadedBlobs.at(-1).text()).diagnostics.heap_free, 1024);
  assert.equal(element('diagnostics_export').disabled, false);
  response = async () => ({ok: false});
  const diagnosticDownloads = downloadLinks.length;
  await context.exportDiagnostics();
  assert.equal(downloadLinks.length, diagnosticDownloads);
  assert.match(banners.at(-1), /Diagnostic export failed/);
  assert.equal(element('diagnostics_export').disabled, false);
  const pressure = {pressure_unit: 'mb', pressure_history: [{epoch: 1791072000, value: 1000.125}, {epoch: 1791073800, value: 1001}]};
  const csv = context.makePressureCsv({...pressure, api_token: 'private-token', wifi_ssid: 'private-network'});
  assert.equal(csv, 'timestamp_utc,pressure_mb\r\n2026-10-04T00:00:00.000Z,1000.125\r\n2026-10-04T00:30:00.000Z,1001\r\n');
  assert.ok(!csv.includes('private'));
  assert.match(context.makePressureCsv({...pressure, pressure_unit: 'inHg', pressure_history: [{epoch: 1791072000, value: 29.92}]}), /pressure_inHg.*\r\n.*29.92/);
  assert.throws(() => context.makePressureCsv({...pressure, pressure_history: []}), /No pressure/);
  assert.throws(() => context.makePressureCsv({...pressure, pressure_unit: '=BAD()'}), /Unsupported/);
  for (const invalid of [{epoch: 1791072000, value: null}, {epoch: 1791072000, value: NaN},
    {epoch: 1791072000, value: -1}, {epoch: '1791072000', value: 1000}, {epoch: 253402300800, value: 1000}])
    assert.throws(() => context.makePressureCsv({...pressure, pressure_history: [invalid]}), /Invalid/);
  assert.throws(() => context.makePressureCsv({...pressure, pressure_history: [pressure.pressure_history[1], pressure.pressure_history[0]]}), /Invalid/);
  response = () => new Promise(resolve => {release = resolve;});
  const exporting = context.exportPressureHistory();
  const exportRequestCount = requests.length;
  await context.exportPressureHistory();
  assert.equal(requests.length, exportRequestCount, 'duplicate exports are blocked');
  assert.equal(element('pressure_export').disabled, true);
  release({ok: true, json: async () => ({telemetry: pressure})});
  await exporting;
  assert.equal(downloadLinks.at(-1), 'weather-pressure-history.csv');
  assert.equal(await downloadedBlobs.at(-1).text(), csv);
  assert.equal(element('pressure_export').disabled, false);
  response = async () => ({ok: false});
  const downloadsBeforeFailure = downloadLinks.length;
  await context.exportPressureHistory();
  assert.equal(downloadLinks.length, downloadsBeforeFailure, 'failed reads do not export cached data');
  assert.match(banners.at(-1), /Pressure export failed/);
  assert.equal(element('pressure_export').disabled, false);
  context.renderForecast({conditions: 'Clear', high: 0, low: -10, feels: null, unit: 'C', source: 'Cloud outlook', age_s: 60});
  assert.equal(element('forecast_conditions').innerText, 'Clear');
  assert.equal(element('forecast_high').innerText, '0.0 C');
  assert.equal(element('forecast_low').innerText, '-10.0 C');
  assert.equal(element('forecast_feels').innerText, '--');
  assert.match(element('forecast_source').innerText, /Downloaded 1m ago/);
  context.renderForecast({conditions: 'Forecast stale', source: 'Forecast stale', age_s: 1200});
  assert.equal(element('forecast_high').innerText, '--');
  assert.equal(element('forecast_conditions').innerText, 'Forecast stale');
  context.renderForecast();
  assert.equal(element('forecast_conditions').innerText, 'Forecast unavailable');
  context.renderCurrent({temperature: -5, temperature_unit: 'C', humidity_pct: 100, dew_point: -5,
    pressure: 1000, pressure_unit: 'mb', source: 'Live sensor data', age_s: 0});
  assert.equal(element('st_temp').innerText, '-5.0 C');
  assert.equal(element('current_humidity').innerText, '100%');
  assert.equal(element('current_dew').innerText, '-5.0 C');
  assert.equal(element('st_press').innerText, '1000.0 mb');
  context.renderCurrent({temperature: 32, temperature_unit: 'F', humidity_pct: 0, dew_point: null,
    pressure: 29.92, pressure_unit: 'inHg'});
  assert.equal(element('current_humidity').innerText, '0%');
  assert.equal(element('current_dew').innerText, '--');
  assert.equal(element('st_press').innerText, '29.92 inHg');
  context.renderCurrent({humidity_pct: 101, pressure: -1, source: 'Stale weather data', age_s: 90});
  assert.equal(element('st_temp').innerText, '--');
  assert.equal(element('current_humidity').innerText, '--');
  assert.equal(element('st_press').innerText, '--');
  assert.match(element('current_source').innerText, /Stale.*1m ago/);
  context.renderEnvironment({uv: 0, solar: 0, battery: 2.5, source: 'Cloud weather data', age_s: 12,
    battery_source: 'Live sensor battery', battery_age_s: 30});
  assert.equal(element('env_uv').innerText, '0.0');
  assert.equal(element('env_solar').innerText, '0 W/m²');
  assert.equal(element('env_battery').innerText, '2.50 V');
  assert.match(element('env_battery_source').innerText, /30s ago/);
  context.renderEnvironment({uv: -1, solar: null, battery: 0, battery_source: 'Sensor battery stale', battery_age_s: 600});
  assert.equal(element('env_uv').innerText, '--');
  assert.equal(element('env_solar').innerText, '--');
  assert.equal(element('env_battery').innerText, '--');
  assert.match(element('env_battery_source').innerText, /stale.*10m ago/);
  context.renderEnvironment();
  assert.equal(element('env_battery').innerText, '--');
  context.renderLightning({count_3h: null, count_today: null, closest: null, last_age_s: null, event_received: false});
  assert.equal(element('lightning_3h').innerText, '--');
  assert.equal(element('lightning_today').innerText, '--');
  assert.equal(element('lightning_last').innerText, 'Not received');
  context.renderLightning({count_3h: 0, count_today: 4, closest: 0, unit: 'km', last_age_s: 0,
    source: 'Receiving sensor observations', event_received: true});
  assert.equal(element('lightning_3h').innerText, '0 strikes');
  assert.equal(element('lightning_today').innerText, '4 strikes');
  assert.equal(element('lightning_closest').innerText, '0.0 km');
  assert.equal(element('lightning_last').innerText, '0s ago');
  context.renderLightning({count_3h: 0, count_today: 0, last_age_s: 172800, event_received: true,
    source: 'Sensor observations stale; counts show received history'});
  assert.equal(element('lightning_last').innerText, '2d 0h ago');
  assert.match(element('lightning_source').innerText, /stale/);
  context.renderLightning({last_age_s: null, event_received: true});
  assert.equal(element('lightning_last').innerText, 'Time unknown');
  context.renderRain({rate: 0, today: 0, yesterday: null, probability_pct: 0, complete: false,
    unit: 'in', rate_unit: 'in/h', source: 'Live sensor data', category: 'Dry', age_s: 5});
  assert.equal(element('rain_rate').innerText, '0.00 in/h');
  assert.equal(element('rain_today').innerText, '0.00 in');
  assert.equal(element('rain_yesterday').innerText, '--');
  assert.equal(element('rain_chance').innerText, '0%');
  assert.match(element('rain_today_label').innerText, /Partial/);
  context.renderRain({rate: null, today: 12.25, yesterday: 2, probability_pct: null, complete: true,
    unit: 'mm', source: 'Rain data stale', age_s: 91});
  assert.equal(element('rain_rate').innerText, '--');
  assert.equal(element('rain_today').innerText, '12.25 mm');
  assert.equal(element('rain_yesterday').innerText, '2.00 mm');
  assert.equal(element('rain_chance').innerText, '--');
  assert.match(element('rain_today_label').innerText, /Complete/);
  assert.match(element('rain_source').innerText, /Rain data stale.*1m ago/);
  context.renderWind({speed: 5.4, gust: 8.6, lull: null, avg: 4, unit: 'mph', direction_deg: 90,
    source: 'Rapid wind', age_s: 2});
  assert.equal(element('wind_speed').innerText, '5 mph');
  assert.equal(element('wind_gust').innerText, '9 mph');
  assert.equal(element('wind_lull').innerText, '--');
  assert.equal(element('wind_direction').innerText, '90 deg');
  assert.match(element('wind_source').innerText, /Rapid wind.*2s ago/);
  context.renderWind({speed: 0, unit: 'km/h', direction_deg: null, calm: true, source: 'Sensor average', age_s: 10});
  assert.equal(element('wind_speed').innerText, '0 km/h');
  assert.equal(element('wind_direction').innerText, 'Calm');
  context.renderWind({speed: null, direction_deg: null, source: 'Wind stale', age_s: 1200});
  assert.equal(element('wind_speed').innerText, '--');
  assert.equal(element('wind_direction').innerText, '--');
  const telemetry = {wifi_connected: true, ip: '192.0.2.1', local_time: '12:00', rssi: -55,
    feed: 'Live sensor data', temp_display: '22 C', press_display: '1000 mb', observation_age_s: 3,
    forecast_fresh: true, forecast_age_s: 0, uptime_s: 3660, firmware_version: '1.0.6', screen: 2,
    clock_synced: true, pressure_unit: 'mb', pressure_sample_age_s: 5,
    pressure_history: [{epoch: 100, value: 1000}, {epoch: 1900, value: 1002}]};
  response = async () => ({ok: true, json: async () => ({settings: {units: 1}, telemetry})});
  vm.runInContext('formInitialized = true', context);
  await context.loadTelemetry();
  assert.equal(element('st_age').innerText, '3s ago');
  assert.equal(element('st_screen').innerText, 'Showing: Rain');
  assert.equal(element('pressure_chart').style.display, 'block');
  assert.match(element('pressure_line').attributes.points, /^48\.0,[\d.]+ 582\.0,[\d.]+$/);
  assert.match(element('pressure_note').innerText, /2 measured samples/);
  context.drawPressure({...telemetry, pressure_history: [{epoch: 100, value: 1000}]}, 1);
  assert.equal(element('pressure_chart').style.display, 'none');
  assert.match(element('pressure_note').innerText, /First sample/);
  response = () => new Promise(resolve => {release = resolve;});
  const loading = context.loadTelemetry();
  assert.equal(element('connection_check').disabled, true);
  assert.equal(element('st_contact').attributes['aria-busy'], 'true');
  const pendingCount = requests.length;
  await context.loadTelemetry();
  assert.equal(requests.length, pendingCount, 'status polls must not overlap');
  release({ok: false});
  await loading;
  assert.equal(element('connection_check').disabled, false);
  assert.equal(element('st_contact').attributes['aria-busy'], 'false');
  assert.equal([...scheduledTimers.values()].filter(timer => timer.callback === context.loadTelemetry).length, 1);
  context.document.hidden = true;
  await documentListeners.visibilitychange();
  assert.equal([...scheduledTimers.values()].filter(timer => timer.callback === context.loadTelemetry).length, 0);
  const hiddenRequests = requests.length;
  await context.loadTelemetry();
  await windowListeners.online();
  assert.equal(requests.length, hiddenRequests, 'hidden tabs do not poll even when the network reconnects');
  context.document.hidden = false;
  response = () => new Promise(resolve => {release = resolve;});
  const resumed = documentListeners.visibilitychange();
  assert.equal(requests.length, hiddenRequests + 1, 'returning to the tab immediately checks the device');
  await windowListeners.online();
  assert.equal(requests.length, hiddenRequests + 1, 'resume and reconnect do not overlap requests');
  context.document.hidden = true;
  await documentListeners.visibilitychange();
  release({ok: false});
  await resumed;
  assert.equal([...scheduledTimers.values()].filter(timer => timer.callback === context.loadTelemetry).length, 0, 'an in-flight read finishing in a hidden tab does not restart polling');
  context.document.hidden = false;
  assert.equal(element('st_temp').innerText, '--', 'offline status must not leave old readings looking live');
  assert.equal(element('net_status').innerText, 'Device unavailable');
  assert.equal(element('st_reset').innerText, '--');
  assert.equal(element('st_heap').innerText, '--');
  assert.equal(element('wind_speed').innerText, '--');
  assert.match(element('wind_source').innerText, /Device unavailable/);
  assert.equal(element('rain_today').innerText, '--');
  assert.equal(element('rain_rate').innerText, '--');
  assert.match(element('rain_source').innerText, /Device unavailable/);
  assert.equal(element('lightning_today').innerText, '--');
  assert.equal(element('lightning_last').innerText, '--');
  assert.match(element('lightning_source').innerText, /Device unavailable/);
  const savedTzInput = element('tz_custom').value;
  element('tz_custom').value = 'unsaved draft';
  response = async () => ({ok: true, json: async () => ({settings: {units: 1}, telemetry})});
  const reconnectRequests = requests.length;
  await windowListeners.online();
  assert.equal(requests.length, reconnectRequests + 1, 'online event immediately refreshes a visible tab');
  assert.equal(element('st_age').innerText, '3s ago');
  assert.equal(element('tz_custom').value, 'unsaved draft', 'recovery preserves unsaved settings');
  element('tz_custom').value = savedTzInput;
  response = async () => ({ok: true, json: async () => ({status: 'ok'})});
  await context.selectScreen(2);
  assert.ok(requests.some(r => r.url === '/view?i=2'));
  assert.match(banners.at(-1), /Display screen changed/);
  await flush();
  element('scroll_weather').checked = true;
  element('scroll_lightning').checked = true;
  assert.equal(context.scrollScreenMask(), 9);
  context.liveSetScrollScreens();
  await flush();
  assert.equal(requests.at(-1).payload.auto_scroll_mask, 9);
  element('station_id').value = '1234';
  element('api_token').value = 'demo-token';
  element('tz_preset').value = '1';
  element('tz_custom').value = 'CST6';
  element('wifi_open').checked = true;
  element('clear_api_token').checked = false;
  element('startup_screen').value = '3';
  element('night_wake_s').value = '60';
  await element('cfg_form').listeners.submit({preventDefault() {}});
  const saved = requests.findLast(r => r.url === '/api/settings').payload;
  assert.equal(saved.station_id, 1234);
  assert.equal(saved.api_token, 'demo-token');
  assert.equal(saved.tz_preset, 1);
  assert.equal(saved.tz_custom, 'CST6');
  assert.equal(saved.wifi_open, true);
  assert.equal(saved.clear_api_token, false);
  assert.equal(saved.startup_screen, 3);
  assert.equal(saved.auto_scroll_mask, 9);
  assert.equal(saved.night_wake_s, 60);
  await flush();
  context.renderCloud({configured: true, state: 'Refresh queued', result: 'Service requested a slower retry',
    http_code: 429, pending: true, busy: false, cooldown: true, retry_in_s: 30, attempt_age_s: 2});
  assert.equal(element('cloud_retry').innerText, 'In 30s');
  assert.match(element('cloud_result').innerText, /429/);
  assert.equal(element('btn_refresh_forecast').disabled, true);
  assert.match(element('cloud_hint').innerText, /retry window/);
  context.renderCloud({configured: true, state: 'Refreshing', busy: true, pending: false, retry_in_s: null});
  assert.equal(element('cloud_retry').innerText, '--');
  assert.equal(element('btn_refresh_forecast').innerText, 'Refreshing...');
  context.renderCloud({configured: false, state: 'Not configured', busy: false, pending: false});
  assert.equal(element('btn_refresh_forecast').disabled, false);
  assert.match(element('cloud_hint').innerText, /station ID/);
  let backupSettings = {units: 0, clock_24h: false, tz_preset: 1, tz_custom: '', brightness_day: 210,
    brightness_dim: 64, brightness_night: 64, dim_timeout_s: 45, auto_scroll_s: 10, startup_screen: 0,
    auto_scroll_mask: 15, screen_rotation: 0, night_mode_enabled: true, night_start_hour: 22,
    night_end_hour: 7, night_wake_s: 30, rotation_supported: false, brightness_min: 64,
    wifi_ssid: 'private-network', wifi_password: 'private-password', station_id: 1234,
    device_serial: 'private-sensor', api_token: 'private-token', admin_password: 'private-admin'};
  const backup = context.makePreferencesBackup(backupSettings, '1.0.9');
  const backupText = JSON.stringify(backup);
  for (const secret of ['private-network', 'private-password', 'private-token', 'private-admin', 'private-sensor', 'station_id'])
    assert.ok(!backupText.includes(secret), 'backup must exclude network, station, and credential fields');
  assert.throws(() => context.validatePreferencesBackup({...backup, format_version: 2}), /Unsupported/);
  assert.throws(() => context.validatePreferencesBackup({...backup, preferences: {...backup.preferences, api_token: 'x'}}), /unsupported/);
  assert.throws(() => context.validatePreferencesBackup({...backup, preferences: {...backup.preferences, clock_24h: 'true'}}), /Invalid/);
  assert.throws(() => context.validatePreferencesBackup({...backup, preferences: {...backup.preferences, screen_rotation: 45}}), /Invalid/);
  const imported = {...backup, preferences: {...backup.preferences, startup_screen: 3, clock_24h: true, screen_rotation: 90, brightness_night: 8}};
  const adapted = context.adaptPreferences(imported.preferences, backupSettings);
  assert.equal(adapted.preferences.screen_rotation, 0);
  assert.equal(adapted.preferences.brightness_night, 64);
  response = async () => ({ok: true, json: async () => ({status: 'ok', settings: backupSettings, telemetry})});
  element('backup_file').files = [{size: 1000, text: async () => JSON.stringify(imported)}];
  const beforePreview = requests.filter(r => r.url === '/api/settings').length;
  await context.previewPreferencesBackup();
  assert.equal(requests.filter(r => r.url === '/api/settings').length, beforePreview, 'preview must not write preferences');
  assert.equal(element('backup_apply').disabled, false);
  assert.ok(element('backup_changes').children.some(c => c.textContent.includes('Clock format: 12-hour → 24-hour')));
  response = async () => ({ok: false, json: async () => ({status: 'error', message: 'Rejected'})});
  await context.applyPreferencesBackup();
  assert.match(banners.at(-1), /Restore failed/);
  assert.equal(element('backup_apply').disabled, false, 'a rejected restore remains available to retry');
  const restorePayload = requests.findLast(r => r.url === '/api/settings').payload;
  assert.equal(restorePayload.screen_rotation, 0);
  assert.equal(restorePayload.clock_24h, true);
  assert.equal(restorePayload.brightness_night, 64);
  assert.ok(!Object.hasOwn(restorePayload, 'wifi_ssid') && !Object.hasOwn(restorePayload, 'api_token'));
  backupSettings = {...backupSettings, ...adapted.preferences};
  response = async () => ({ok: true, json: async () => ({status: 'ok', settings: backupSettings, telemetry})});
  await context.applyPreferencesBackup();
  assert.match(banners.at(-1), /Preferences restored/);
  assert.equal(element('backup_preview').style.display, 'none');
  assert.equal(element('clock_24h').checked, true);
  element('backup_file').files = [{size: 20000, text: async () => '{}'}];
  const beforeOversized = requests.length;
  await context.previewPreferencesBackup();
  assert.match(banners.at(-1), /too large/);
  assert.equal(requests.length, beforeOversized);
  // Work from the initialized saved form. Polling must leave local edits alone.
  assert.equal(context.hasUnsavedSettings(), false);
  element('device_serial').value = 'ST-new';
  element('cfg_form').listeners.input();
  assert.equal(context.hasUnsavedSettings(), true);
  assert.match(element('settings_state').innerText, /Unsaved changes/);
  let prevented = false;
  windowListeners.beforeunload({preventDefault() {prevented = true;}});
  assert.equal(prevented, true);
  response = async () => ({ok: true, json: async () => ({status: 'ok', settings: backupSettings, telemetry})});
  await context.loadTelemetry();
  assert.equal(element('device_serial').value, 'ST-new', 'polling preserves edits');
  const beforeGuard = requests.length;
  await context.restartDevice();
  await context.performOTA();
  vm.runInContext('pendingPreferenceRestore = {clock_24h: false}', context);
  await context.applyPreferencesBackup();
  assert.equal(requests.length, beforeGuard, 'unsaved edits block restore, reboot and OTA');
  assert.match(banners.at(-1), /Save your unsaved/);

  response = () => new Promise(resolve => {release = resolve;});
  const pendingSave = element('cfg_form').listeners.submit({preventDefault() {}});
  const countPending = requests.length;
  assert.equal(element('btn_save_settings').disabled, true);
  await element('cfg_form').listeners.submit({preventDefault() {}});
  assert.equal(requests.length, countPending, 'duplicate full saves are blocked');
  element('device_serial').value = 'ST-newer';
  release({ok: true, json: async () => ({status: 'ok'})});
  await pendingSave;
  // Full save starts a status fetch. Release it without modifying any form values.
  release({ok: true, json: async () => ({settings: backupSettings, telemetry})});
  await flush();
  assert.equal(element('device_serial').value, 'ST-newer');
  assert.equal(context.hasUnsavedSettings(), true, 'edits made during save remain unsaved');
  assert.equal(element('btn_save_settings').disabled, false);
  response = async () => {throw new Error('Connection lost');};
  await element('cfg_form').listeners.submit({preventDefault() {}});
  assert.equal(element('device_serial').value, 'ST-newer');
  assert.equal(context.hasUnsavedSettings(), true);
  assert.match(banners.at(-1), /Save not confirmed/);
  assert.equal(element('btn_save_settings').disabled, false);
  response = async () => ({ok: true, json: async () => ({status: 'ok', settings: backupSettings, telemetry})});
  await element('cfg_form').listeners.submit({preventDefault() {}});
  await flush();
  assert.equal(context.hasUnsavedSettings(), false);
  prevented = false;
  windowListeners.beforeunload({preventDefault() {prevented = true;}});
  assert.equal(prevented, false, 'clean form does not warn when leaving');

  element('clock_24h').checked = false;
  response = async () => ({ok: false, json: async () => ({message: 'Rejected'})});
  await context.sendLive({clock_24h: false, save: true});
  assert.equal(context.hasUnsavedSettings(), true, 'rejected live saves remain dirty');
  response = async () => ({ok: true, json: async () => ({status: 'ok'})});
  await context.sendLive({clock_24h: false, save: true});
  assert.equal(context.hasUnsavedSettings(), false, 'acknowledged live saves clear only their fields');
  element('wifi_pass').value = 'pending-edit';
  element('clock_24h').checked = true;
  await context.sendLive({clock_24h: true, save: true});
  assert.equal(context.hasUnsavedSettings(), true, 'live save preserves unrelated pending edits');
  element('wifi_pass').value = '';
  // A reboot rejection must not claim the reboot was triggered.
  context.confirm = () => true;
  response = async () => ({ok: false});
  await context.restartDevice();
  assert.match(banners.at(-1), /Restart not confirmed/);
  element('wifi_pass').value = ' password with spaces ';
  element('api_token').value = ' example-token ';
  response = async () => ({ok: true, json: async () => ({status: 'ok', settings: backupSettings, telemetry})});
  await element('cfg_form').listeners.submit({preventDefault() {}});
  await flush();
  assert.equal(requests.findLast(r => r.url === '/api/settings').payload.wifi_pass, ' password with spaces ');
  assert.equal(requests.findLast(r => r.url === '/api/settings').payload.api_token, 'example-token');
  assert.equal(element('wifi_pass').value, '', 'acknowledged passwords clear without changing their whitespace');
  assert.equal(element('api_token').value, '');
  assert.equal(context.hasUnsavedSettings(), false);
  console.log('Unsaved edit protection, save concurrency, in-flight edits, failed saves, live acknowledgements and reboot rejection tests passed');
  console.log('Preference backup exclusion, validation, preview, adaptation, failed restore, and successful restore tests passed');
  console.log('Dashboard saves, freshness, measured pressure history, offline state, and screen-control tests passed');
})().catch(error => {console.error(error); process.exitCode = 1;});
