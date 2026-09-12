#!/usr/bin/env python3
"""Collect the NativeKit Haxeon browser benchmark through Chrome DevTools."""

import argparse
import json
import time
import urllib.request

from web_smoke import WebSocket


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug-port", type=int, required=True)
    parser.add_argument("--page-url", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    page = None
    while time.monotonic() < deadline:
        pages = json.load(
            urllib.request.urlopen(f"http://127.0.0.1:{args.debug_port}/json/list")
        )
        page = next((item for item in pages if item.get("url") == args.page_url), None)
        if page:
            break
        time.sleep(0.1)
    if not page:
        raise RuntimeError(f"Chrome page did not open: {args.page_url}")

    websocket = WebSocket(page["webSocketDebuggerUrl"])
    try:
        while time.monotonic() < deadline:
            value = websocket.evaluate(
                "JSON.stringify({result:document.documentElement.dataset.nativekitResult||'',"
                "report:document.documentElement.dataset.nativekitBenchmark||''})",
                1,
            )
            state = json.loads(value)
            if state["result"]:
                if state["result"] != "0":
                    raise RuntimeError(f"browser benchmark failed with result {state['result']}")
                if not state["report"]:
                    raise RuntimeError("browser benchmark completed without a report")
                report = json.loads(state["report"])
                with open(args.output, "w", encoding="utf-8") as output:
                    json.dump(report, output, indent=2)
                    output.write("\n")
                frames = report["frames"]
                print(
                    f"web benchmark passed: median={frames['median_ms']:.3f} ms "
                    f"p95={frames['p95_ms']:.3f} ms p99={frames['p99_ms']:.3f} ms "
                    f"estimated_dropped={report['estimated_dropped_frames']} "
                    f"JSON={args.output}"
                )
                return
            time.sleep(0.1)
        raise RuntimeError("browser benchmark timed out")
    finally:
        websocket.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"web benchmark failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
