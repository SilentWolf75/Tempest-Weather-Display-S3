"""Package fresh builds for both boards; optionally update the checked-in web installer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from release_inputs import source_digest

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [("esp32-s3-amoled-175", "ESP32-S3", ""), ("esp32-p4-lcd-4c", "ESP32-P4", "p4")]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--web", action="store_true", help="Update local installer assets after both builds pass")
    args = parser.parse_args()
    version = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', (ROOT / "src/config.h").read_text(encoding="utf-8")).group(1)
    builds = []
    inputs = source_digest(ROOT)
    for environment, family, subdir in TARGETS:
        metadata_file = ROOT / ".pio/build" / environment / "release_metadata.json"
        metadata = json.loads(metadata_file.read_text(encoding="utf-8"))
        if metadata["version"] != version:
            raise SystemExit(f"Rebuild {environment}: its version does not match {version}")
        if metadata.get("source_sha256") != inputs:
            raise SystemExit(f"Rebuild {environment}: firmware inputs have changed")
        for part in metadata["parts"]:
            path = Path(part["path"])
            if not path.is_absolute(): path = ROOT / path
            if not path.is_file(): raise SystemExit(f"Missing build artifact: {path}")
            part["source"] = path
        builds.append((metadata, family, subdir))
    destination = ROOT / "dist" / f"release-v{version}"
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {"name": "Tempest Weather Display S3 & P4", "version": version,
                "new_install_prompt_erase": True, "builds": []}
    hashes = {}
    for metadata, family, subdir in builds:
        parts = []
        folder = destination / subdir
        folder.mkdir(parents=True, exist_ok=True)
        merge_args = []
        for part in metadata["parts"]:
            name = "firmware.bin" if part["offset"] == 65536 else part["source"].name
            out = folder / name
            shutil.copy2(part["source"], out)
            relative = out.relative_to(destination).as_posix()
            parts.append({"path": relative, "offset": part["offset"]})
            hashes[relative] = hashlib.sha256(out.read_bytes()).hexdigest()
            merge_args.extend([hex(part["offset"]), str(out)])
        factory = folder / ("tempest-weather-s3-factory.bin" if not subdir else "tempest-weather-p4-factory.bin")
        subprocess.run([metadata["python"], metadata["esptool"], "--chip", metadata["chip"], "merge_bin", "-o", str(factory)] + merge_args, check=True, cwd=ROOT)
        hashes[factory.relative_to(destination).as_posix()] = hashlib.sha256(factory.read_bytes()).hexdigest()
        manifest["builds"].append({"chipFamily": family, "parts": parts})
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (destination / "SHA256SUMS.json").write_text(json.dumps(hashes, indent=2) + "\n", encoding="utf-8")
    (destination / "version.json").write_text(json.dumps({"version": version}) + "\n", encoding="utf-8")
    (destination / "build.json").write_text(json.dumps({"version": version, "source_sha256": inputs}) + "\n", encoding="utf-8")
    if args.web:
        shutil.copytree(destination, ROOT / "web", dirs_exist_ok=True)
    print(f"Packaged v{version} for S3 and P4: {destination}")

if __name__ == "__main__":
    main()
