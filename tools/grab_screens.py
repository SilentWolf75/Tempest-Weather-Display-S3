#!/usr/bin/env python3
"""Capture screenshots from running Tempest Weather Display and save them as PNGs.
Each screenshot has its corners made transparent to match the round 466x466 AMOLED panel.
Pure Python standard library (no Pillow required).
"""
import os
import struct
import sys
import time
import urllib.request
import zlib

SCREENS = [
    (0, "screen_main",      "Current Weather Conditions"),
    (1, "screen_wind",      "360-Degree Wind Compass"),
    (2, "screen_lightning", "Lightning Strike Radar"),
    (3, "screen_info",      "Device & Network Diagnostics"),
]

def get(url, timeout=15):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read()

def read_bmp(data):
    if data[:2] != b"BM":
        raise ValueError("Not a valid BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 24:
        raise ValueError(f"Expected 24-bit BMP, got {bpp}")
    row_bytes = w * 3
    pad = (4 - (row_bytes % 4)) % 4
    rows = []
    for y in range(h):
        start = off + y * (row_bytes + pad)
        rows.append(data[start:start + row_bytes])
    rows.reverse() # BMP stores bottom-up
    return w, h, rows

def write_png(path, w, h, rows, round_mask=True):
    cx = cy = (w - 1) / 2.0
    r2 = (min(w, h) / 2.0) ** 2
    raw = bytearray()
    for y in range(h):
        raw.append(0) # filter: none
        src = rows[y]
        for x in range(w):
            b, g, r = src[x * 3], src[x * 3 + 1], src[x * 3 + 2]
            a = 255
            if round_mask and ((x - cx) ** 2 + (y - cy) ** 2) > r2:
                a = 0
            raw += bytes((r, g, b, a))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return len(png)

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.79"
    out_dirs = [
        os.path.join("docs", "img", "screens"),
        os.path.join("web", "img", "screens"),
    ]
    for d in out_dirs:
        os.makedirs(d, exist_ok=True)

    print(f"Connecting to http://{host}/ to grab live screenshots...")
    for idx, name, label in SCREENS:
        # Switch to view
        try:
            get(f"http://{host}/view?i={idx}")
            time.sleep(1.8) # Wait for animation/render
        except Exception as e:
            print(f"  Error switching to screen {idx} ({name}): {e}")
            continue

        try:
            bmp_data = get(f"http://{host}/shot.bmp")
            w, h, rows = read_bmp(bmp_data)
            for d in out_dirs:
                dest = os.path.join(d, f"{name}.png")
                nbytes = write_png(dest, w, h, rows, round_mask=True)
            print(f"  [OK] Screen {idx}: {label:30} -> {name}.png ({w}x{h}, {nbytes/1024:.1f} KB)")
        except Exception as e:
            print(f"  Error capturing screen {idx} ({name}): {e}")

    # Return to main screen
    try:
        get(f"http://{host}/view?i=0")
    except Exception:
        pass
    print("Done grabbing screenshots.")

if __name__ == "__main__":
    main()
