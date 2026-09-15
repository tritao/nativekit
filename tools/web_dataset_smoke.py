#!/usr/bin/env python3
"""Run dataset-marked NativeKit browser integration pages through CDP."""

import argparse
import json
import time

from web_smoke import WebSocket, wait_for_page


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug-port", type=int, required=True)
    parser.add_argument("--page-url", required=True,
                        help="the currently open page used by the browser session")
    parser.add_argument("--test-page", action="append", required=True)
    parser.add_argument("--dataset-key", action="append", required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    if len(args.test_page) != len(args.dataset_key):
        raise RuntimeError("each --test-page needs one --dataset-key")

    page = wait_for_page(args.debug_port, args.page_url, args.timeout)
    websocket = WebSocket(page["webSocketDebuggerUrl"])
    try:
        websocket.command("Runtime.enable", {}, 20)
        websocket.command("Page.enable", {}, 21)
        for index, (test_page, dataset_key) in enumerate(zip(args.test_page, args.dataset_key)):
            websocket.errors.clear()
            websocket.command("Page.navigate", {"url": test_page}, 30 + index * 10)
            deadline = time.monotonic() + args.timeout
            result = ""
            while time.monotonic() < deadline:
                value = websocket.evaluate(
                    "document.documentElement?.dataset[%s] || ''" % json.dumps(dataset_key),
                    40 + index,
                )
                result = value or ""
                if result:
                    break
                time.sleep(0.1)
            if result != "passed":
                raise RuntimeError(
                    f"browser integration {test_page} returned {dataset_key}={result!r}"
                )
            if websocket.errors:
                raise RuntimeError("browser reported errors: " + json.dumps(websocket.errors))
            print(f"web integration passed: {test_page}")
        return 0
    finally:
        websocket.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"web integration failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
