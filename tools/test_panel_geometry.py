#!/usr/bin/env python3
"""Smoke-test awtrix-linux canvas sizing for tall panels.

The ESP32 takes its height from the panelHeight setting (8-16, applied after a reboot); the
Linux app takes it from --width/--height and offers no panel settings. This starts the
Linux app at each size and checks that the canvas, the screen API and the capabilities agree.

Run after the Linux build, in the build container:
    python tools/test_panel_geometry.py [--binary .pio/linux/awtrix-linux] [53x11 32x16 ...]
Uses only an isolated temporary data directory and a loopback HTTP port.
"""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"))
from runtime import Runtime  # noqa: E402

DEFAULT_SIZES = ("32x8", "53x11", "32x16")


def size(text):
    width, height = (int(part) for part in text.lower().split("x"))
    return width, height


def start(directory, binary, webui, width, height):
    app = Runtime(Path(directory), binary, webui)
    app.width, app.height = width, height
    app.start()
    return app


def check(binary, webui, width, height):
    with tempfile.TemporaryDirectory(prefix="awtrix-geometry-") as directory:
        app = start(directory, binary, webui, width, height)
        try:
            frame = app.screen()
            assert (frame["width"], frame["height"]) == (width, height), (frame["width"], frame["height"])
            assert len(frame["pixels"]) == width * height, len(frame["pixels"])
            display = app.json("GET", "/api/v1/capabilities")["display"]
            assert (display["width"], display["height"]) == (width, height), display
            assert display["configurable"] is False, display
            system = app.json("GET", "/api/v1/system")
            assert "panelHeight" not in system and "panelWidth" not in system, sorted(system)
            print(f"PASS screen {width}x{height}: {len(frame['pixels'])} pixels, "
                  "capabilities agree, no panel settings on Linux")
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
    print("PASS panel geometry")


if __name__ == "__main__":
    main()
