#!/usr/bin/env python3
"""Sample every effect on awtrix-linux at one or more canvas sizes; write PNG strips and tables.

Run after the Linux build, in the build container, from the repository root:
    python tools/linux/effect_survey.py --size 32x8 --size 53x11 --out .pio/effect-survey
Each size starts its own awtrix-linux (--width/--height) with an isolated temporary data
directory and a loopback port, holds an empty notification with each effect from
/api/v1/capabilities and samples /api/v1/display/screen. Samples are wall-clock
observations, not golden images or a complete animation cycle. Standard library only.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tests"))
from runtime import Runtime  # noqa: E402


def png(path, rows_of_frames, width, height, scale, gap=1):
    """One row of frames per effect, frames left to right, a dark gap between them."""
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    count = max(len(frames) for frames in rows_of_frames)
    out_w = (count * (width + gap) - gap) * scale
    raw = bytearray()
    for index, frames in enumerate(rows_of_frames):
        for y in range(height):
            row = bytearray(b"\0")
            for n in range(count):
                frame = frames[n] if n < len(frames) else (0,) * (width * height)
                for pixel in frame[y * width:(y + 1) * width]:
                    row.extend(bytes(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255)) * scale)
                if n + 1 < count:
                    row.extend(b"\x30\x30\x30" * gap * scale)
            raw.extend(row * scale)
        if index + 1 < len(rows_of_frames):
            raw.extend((b"\0" + b"\x30\x30\x30" * out_w) * gap * scale)
    out_h = (len(rows_of_frames) * (height + gap) - gap) * scale
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", out_w, out_h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def survey(args, width, height):
    out = args.out / f"{width}x{height}"
    out.mkdir(parents=True, exist_ok=True)
    report, strips = [], []
    with tempfile.TemporaryDirectory(prefix="awtrix-effects-") as directory:
        app = Runtime(Path(directory), args.binary, args.webui)
        app.width, app.height = width, height
        app.start()
        try:
            effects = app.json("GET", "/api/v1/capabilities")["effects"]
            for effect in effects:
                app.request("DELETE", "/api/v1/notifications/active")
                app.json("POST", "/api/v1/notifications",
                         {"name": "effect-survey", "text": "", "hold": True, "effect": effect})
                time.sleep(args.settle)
                frames = []
                for _ in range(args.frames):
                    screen = app.screen()
                    assert (screen["width"], screen["height"]) == (width, height), screen
                    assert len(screen["pixels"]) == width * height
                    frames.append(tuple(screen["pixels"]))
                    time.sleep(args.interval)
                lit = [y for y in range(height) if any(any(f[y * width:(y + 1) * width]) for f in frames)]
                changing = [y for y in range(height) if len({f[y * width:(y + 1) * width] for f in frames}) > 1]
                png(out / (re.sub(r"[^A-Za-z0-9_-]", "_", effect) + ".png"), [frames], width, height, args.scale)
                strips.append(frames)
                report.append({"effect": effect, "distinct": len(set(frames)), "lit": lit, "changing": changing})
        finally:
            app.stop()
    png(out / "all.png", strips, width, height, args.scale)
    table = "| Effect | Distinct | Lit rows (0-based) | Changing rows |\n|---|---:|---|---|\n"
    for row in report:
        table += f"| {row['effect']} | {row['distinct']} | {row['lit']} | {row['changing']} |\n"
    (out / "summary.md").write_text(table)
    (out / "summary.json").write_text(json.dumps({"size": [width, height], "frames": args.frames,
        "interval": args.interval, "settle": args.settle, "effects": report}, indent=2) + "\n")
    print(f"\n{width}x{height} (all.png rows, top to bottom: {', '.join(r['effect'] for r in report)})\n{table}",
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, default=Path(".pio/linux/awtrix-linux"))
    parser.add_argument("--webui", type=Path, default=Path("webui/index.html"))
    parser.add_argument("--out", type=Path, default=Path(".pio/effect-survey"))
    parser.add_argument("--size", action="append", default=[])
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--interval", type=float, default=.25)
    parser.add_argument("--settle", type=float, default=.5)
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()
    if args.frames < 2 or args.scale < 1 or args.interval <= 0 or args.settle < 0:
        parser.error("frames >= 2, scale >= 1, interval > 0 and settle >= 0 required")
    for size in args.size or ["32x8", "53x11"]:
        if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", size):
            parser.error("size must be WIDTHxHEIGHT")
        survey(args, *map(int, size.split("x")))


if __name__ == "__main__":
    main()
