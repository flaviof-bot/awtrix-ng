#!/usr/bin/env python3
"""Render an icon/text notification on a tall awtrix-linux canvas and print it as ASCII.

The notification's eight-row content must sit in the centred band that starts at row
floor((H - 8) / 2), with every row outside it dark. Run after the Linux build, in the
build container:
    python tools/test_tall_notification.py [--binary .pio/linux/awtrix-linux] [53x11 32x16 ...]
No network assets or third-party Python packages are needed.
"""
import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))
from runtime import Runtime  # noqa: E402

DEFAULT_SIZES = ("53x11", "32x16")


def size(text):
    width, height = (int(part) for part in text.lower().split("x"))
    return width, height


def gif_fixture():
    spec = importlib.util.spec_from_file_location("gif_fixture", ROOT / "scripts/test_dynamic_gif_api.py")
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    return fixture


def upload_icon(app, name, data):
    body = (b'--tall\r\nContent-Disposition: form-data; name="file"; filename="' + name.encode() +
            b'.gif"\r\nContent-Type: image/gif\r\n\r\n' + data + b'\r\n--tall--\r\n')
    status, response = app.request("POST", "/api/v1/files?dir=/ICONS", body,
                                   "multipart/form-data; boundary=tall")
    assert status == 200, (status, response)


def check(binary, webui, width, height):
    top = (height - 8) // 2 if height > 8 else 0
    band = range(top, top + 8)
    with tempfile.TemporaryDirectory(prefix="awtrix-tall-notification-") as directory:
        app = Runtime(Path(directory), binary, webui)
        app.width, app.height = width, height
        app.start()
        try:
            # The page zoom (if this size has one) would enlarge the page instead of banding it.
            app.json("PATCH", "/api/v1/settings", {"autoTransition": False, "enlargeApps": False})
            upload_icon(app, "tallicon", gif_fixture().animated_gif(8, 8, colors=(0xFFFFFF, 0xFFFFFF)))
            app.json("POST", "/api/v1/notifications",
                     {"text": "HELLO", "icon": "tallicon", "hold": True,
                      "textColor": "#FFFFFF", "backgroundColor": "#000000"})
            deadline = time.monotonic() + 20
            rows = []
            while time.monotonic() < deadline:
                frame = app.screen()
                assert (frame["width"], frame["height"]) == (width, height)
                pixels = frame["pixels"]
                assert len(pixels) == width * height
                rows = [pixels[y * width:(y + 1) * width] for y in range(height)]
                outside = [rows[y] for y in range(height) if y not in band]
                inside = [rows[y] for y in band]
                if (not any(any(row) for row in outside)
                        and all(all(row[:8]) for row in inside)
                        and any(any(row[9:]) for row in inside)):
                    print(f"PASS notification screen {width}x{height}: icon/text in rows "
                          f"{top}-{top + 7}")
                    for row in rows:
                        print("".join("#" if pixel else "." for pixel in row))
                    return
                time.sleep(0.1)
            for row in rows:
                print("".join("#" if pixel else "." for pixel in row))
            raise AssertionError(f"{width}x{height}: notification did not settle into rows "
                                 f"{top}-{top + 7}")
        finally:
            app.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=".pio/linux/awtrix-linux")
    parser.add_argument("--webui", default="webui/index.html")
    parser.add_argument("sizes", nargs="*", type=size, default=[size(s) for s in DEFAULT_SIZES])
    args = parser.parse_args()
    for width, height in args.sizes:
        check(args.binary, args.webui, width, height)


if __name__ == "__main__":
    main()
