Import("env")
import re
import json
import sys
from pathlib import Path

source = Path(env.subst("$PROJECT_DIR")) / "src/config.h"
sys.path.insert(0, str(source.parents[1] / "tools"))
from release_inputs import source_digest
input_digest = source_digest(source.parents[1])
version = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', source.read_text(encoding="utf-8")).group(1)
env.Append(CPPDEFINES=[("BUILD_VERSION", env.StringifyMacro(version))])

def save_metadata(source, target, env):
    if source_digest(env.subst("$PROJECT_DIR")) != input_digest:
        raise RuntimeError("Firmware sources changed during the build; rebuild before packaging")
    parts = [{"offset": int(str(offset), 0), "path": env.subst(str(path))}
             for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    parts.append({"offset": int(env.subst("$ESP32_APP_OFFSET"), 0), "path": str(target[0])})
    result = {"version": version, "source_sha256": input_digest, "environment": env.subst("$PIOENV"),
              "chip": env.BoardConfig().get("build.mcu"), "parts": parts,
              "python": env.subst("$PYTHONEXE"), "esptool": env.subst("$UPLOADER")}
    Path(env.subst("$BUILD_DIR/release_metadata.json")).write_text(json.dumps(result, indent=2), encoding="utf-8")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", save_metadata)
