#!/usr/bin/env python3
"""Capture and compare a deterministic NativeKit WebGL framebuffer."""

import argparse
import base64
import json
import pathlib
import struct
import time
import urllib.request
import zlib

from web_smoke import WebSocket


def read_png(data, source):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"unsupported PNG image: {source}")
    offset = 8
    compressed = bytearray()
    width = height = color_type = None
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if depth != 8 or color_type not in (2, 6) or compression or filtering or interlace:
                raise RuntimeError(f"unsupported PNG format: {source}")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    channels = 3 if color_type == 2 else 4
    scanlines = zlib.decompress(compressed)
    stride = width * channels
    pixels = bytearray(width * height * channels)
    previous = bytearray(stride)
    cursor = 0
    for y in range(height):
        filter_kind = scanlines[cursor]
        cursor += 1
        row = bytearray(scanlines[cursor:cursor + stride])
        cursor += stride
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            above = previous[x]
            upper_left = previous[x - channels] if x >= channels else 0
            if filter_kind == 1:
                row[x] = (row[x] + left) & 255
            elif filter_kind == 2:
                row[x] = (row[x] + above) & 255
            elif filter_kind == 3:
                row[x] = (row[x] + ((left + above) // 2)) & 255
            elif filter_kind == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
                row[x] = (row[x] + (left, above, upper_left)[distances.index(min(distances))]) & 255
            elif filter_kind != 0:
                raise RuntimeError(f"unsupported PNG filter: {filter_kind}")
        pixels[y * stride:(y + 1) * stride] = row
        previous = row
    rgb = bytearray(width * height * 3)
    for index in range(width * height):
        rgb[index * 3:index * 3 + 3] = pixels[index * channels:index * channels + 3]
    return width, height, rgb


def write_png(path, width, height, pixels):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    scanlines = b"".join(
        b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
        for y in range(height)
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(scanlines, 9)) +
        chunk(b"IEND", b"")
    )


def compare(reference, width, height, actual, tolerance, allowed_ratio):
    expected_width, expected_height, expected = read_png(reference.read_bytes(), reference)
    if (width, height) != (expected_width, expected_height):
        raise RuntimeError(
            f"image size changed: expected {expected_width}x{expected_height}, got {width}x{height}"
        )
    mismatched = 0
    largest_delta = 0
    for y in range(height):
        for x in range(width):
            source = (y * width + x) * 3
            delta = max(abs(actual[source + channel] - expected[source + channel]) for channel in range(3))
            largest_delta = max(largest_delta, delta)
            if delta > tolerance:
                mismatched += 1
    ratio = mismatched / (width * height)
    return mismatched, ratio, largest_delta, expected


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug-port", type=int, required=True)
    parser.add_argument("--page-url", required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--reference", type=pathlib.Path, required=True)
    parser.add_argument("--artifact-dir", type=pathlib.Path, required=True)
    parser.add_argument("--click")
    parser.add_argument("--expect-offset", type=int)
    parser.add_argument("--expect-affinity", type=int)
    parser.add_argument("--expect-direction", type=int)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--tolerance", type=int, default=12)
    parser.add_argument("--allowed-ratio", type=float, default=0.005)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    page = None
    while time.monotonic() < deadline:
        pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{args.debug_port}/json/list"))
        page = next((item for item in pages if item.get("url") == args.page_url), None)
        if page:
            break
        time.sleep(0.1)
    if not page:
        raise RuntimeError(f"Chrome page did not open: {args.page_url}")

    websocket = WebSocket(page["webSocketDebuggerUrl"])
    websocket.socket.settimeout(args.timeout)
    try:
        state = None
        while time.monotonic() < deadline:
            try:
                value = websocket.evaluate(
                    "JSON.stringify({frames:Number(document.documentElement?.dataset.nativekitFrames||0),"
                    "result:document.documentElement?.dataset.nativekitResult||'',"
                    "width:document.getElementById('canvas')?.width||0,"
                    "height:document.getElementById('canvas')?.height||0})",
                    1,
                )
            except (OSError, RuntimeError):
                websocket.close()
                time.sleep(0.05)
                pages = json.load(
                    urllib.request.urlopen(f"http://127.0.0.1:{args.debug_port}/json/list")
                )
                page = next((item for item in pages if item.get("url") == args.page_url), None)
                if page:
                    websocket = WebSocket(page["webSocketDebuggerUrl"])
                    websocket.socket.settimeout(args.timeout)
                continue
            state = json.loads(value)
            if state["result"] and state["result"] != "0":
                raise RuntimeError(f"showcase failed before capture: {state}")
            if state["frames"] >= 3 and state["width"] == args.width and state["height"] == args.height:
                break
            time.sleep(0.05)
        else:
            raise RuntimeError(f"showcase did not reach the requested visual frame: {state}")

        if args.click:
            click_x, click_y = (float(value) for value in args.click.split(",", 1))
            websocket.command("Input.dispatchMouseEvent", {
                "type": "mousePressed", "x": click_x, "y": click_y,
                "button": "left", "buttons": 1, "clickCount": 1,
            }, 3)
            websocket.command("Input.dispatchMouseEvent", {
                "type": "mouseReleased", "x": click_x, "y": click_y,
                "button": "left", "buttons": 0, "clickCount": 1,
            }, 4)
            target_frames = state["frames"] + 3
            while time.monotonic() < deadline:
                frames = websocket.evaluate(
                    "Number(document.documentElement.dataset.nativekitFrames||0)", 5
                )
                if frames >= target_frames:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("showcase did not render the requested interaction")

        caret = websocket.evaluate(
            "({offset:guest['ShowcaseWeb.caretOffset'](),"
            "affinity:guest['ShowcaseWeb.caretAffinity'](),"
            "direction:guest['ShowcaseWeb.caretDirection']()})", 6)
        for field, expected in (("offset", args.expect_offset),
                                ("affinity", args.expect_affinity),
                                ("direction", args.expect_direction)):
            if expected is not None and caret[field] != expected:
                raise RuntimeError(f"caret {field} changed: expected {expected}, got {caret[field]}")

        screenshot = websocket.command(
            "Page.captureScreenshot",
            {"format": "png", "fromSurface": True, "captureBeyondViewport": True},
            2,
        )
        png = base64.b64decode(screenshot["data"])
        width, height, pixels = read_png(png, "Chrome screenshot")
        if (width, height) != (args.width, args.height):
            raise RuntimeError(f"screenshot size changed: expected {args.width}x{args.height}, got {width}x{height}")
        if args.update or not args.reference.exists():
            args.reference.parent.mkdir(parents=True, exist_ok=True)
            args.reference.write_bytes(png)
            print(f"updated visual baseline: {args.reference}")
        else:
            mismatched, ratio, largest, expected = compare(
                args.reference, width, height, pixels,
                args.tolerance, args.allowed_ratio,
            )
            if ratio > args.allowed_ratio:
                stem = args.reference.stem
                actual_path = args.artifact_dir / f"{stem}-actual.png"
                diff_path = args.artifact_dir / f"{stem}-diff.png"
                actual_path.parent.mkdir(parents=True, exist_ok=True)
                actual_path.write_bytes(png)
                difference = bytearray(len(pixels))
                for index in range(0, len(pixels), 3):
                    delta = max(abs(pixels[index + channel] - expected[index + channel])
                                for channel in range(3))
                    difference[index:index + 3] = (bytes((255, 32, 96)) if delta > args.tolerance
                                                    else bytes(value // 4 for value in pixels[index:index + 3]))
                write_png(diff_path, width, height, difference)
                raise RuntimeError(
                    f"visual mismatch: {mismatched} pixels ({ratio:.3%}) exceed tolerance "
                    f"{args.tolerance}; allowed {args.allowed_ratio:.3%}, largest delta {largest}; "
                    f"wrote {actual_path} and {diff_path}"
                )
            print(
                f"visual match: {args.reference.name}: {mismatched} pixels ({ratio:.3%}), "
                f"largest delta {largest}; caret {caret}"
            )
    finally:
        websocket.close()


if __name__ == "__main__":
    raise SystemExit(main())
