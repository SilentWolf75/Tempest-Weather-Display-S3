"""Compile and run the actual firmware state/settings/brightness code on the host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++") or shutil.which("cl")
if not compiler:
    raise SystemExit("Run in a Visual Studio Developer shell on Windows, or install g++/clang++.")
sources = [ROOT / p for p in ["tests/host/weather_tests.cpp", "src/tempest_state.cpp", "src/settings_mgr.cpp", "src/brightness_mgr.cpp"]]
web = (ROOT / "src/web_config_server.cpp").read_text(encoding="utf-8")
def section(start, end):
    return web[web.index(start):web.index(end)]
handlers = "\n".join([
    section("static bool authorized(", "extern const char dashboard_html_start"),
    section("static void handle_api_settings()", "static void handle_api_wifi_scan()"),
    section("static void handle_view()", "static void handle_shot_bmp()"),
    section("static void ota_reset()", "void web_config_server_network_changed()"),
])
json_include = ROOT / ".pio/libdeps/esp32-s3-amoled-175/ArduinoJson/src"
if not json_include.exists():
    raise SystemExit("Build the S3 target first to install its pinned ArduinoJson headers.")
with tempfile.TemporaryDirectory(prefix="weather-tests-") as temp:
    (Path(temp) / "web_handlers.inc").write_text(handlers, encoding="utf-8")
    udp = (ROOT / "src/tempest_udp.cpp").read_text(encoding="utf-8")
    (Path(temp) / "udp_observation.inc").write_text(
        udp[udp.index("static void handle_obs_st("):udp.index("static void handle_precip(")], encoding="utf-8")
    exe = Path(temp) / ("weather-tests.exe" if os.name == "nt" else "weather-tests")
    if Path(compiler).stem.lower() == "cl":
        args = [compiler, "/nologo", "/std:c++17", "/EHsc", "/D_CRT_SECURE_NO_WARNINGS", "/DBOARD_WAVESHARE_S3_AMOLED_175",
                "/I" + str(ROOT / "tests/host"), "/I" + str(ROOT / "src"), "/Fe:" + str(exe)]
    else:
        args = [compiler, "-std=c++17", "-pthread", "-DBOARD_WAVESHARE_S3_AMOLED_175", "-I" + str(ROOT / "tests/host"),
                "-I" + str(ROOT / "src"), "-o", str(exe)]
    subprocess.run(args + list(map(str, sources)), cwd=temp, check=True)
    subprocess.run([str(exe)], check=True)
    web_exe = Path(temp) / ("web-tests.exe" if os.name == "nt" else "web-tests")
    if Path(compiler).stem.lower() == "cl":
        args = [a for a in args if not a.startswith("/Fe:")] + ["/Fe:" + str(web_exe), "/I" + temp, "/I" + str(json_include)]
    else:
        args = args[:-2] + ["-I" + temp, "-I" + str(json_include), "-o", str(web_exe)]
    subprocess.run(args + [str(ROOT / "tests/host/web_tests.cpp")] + list(map(str, sources[1:])), cwd=temp, check=True)
    subprocess.run([str(web_exe)], check=True)
subprocess.run(["node", str(ROOT / "tests/dashboard.test.cjs")], cwd=ROOT, check=True)
