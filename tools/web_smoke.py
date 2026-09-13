#!/usr/bin/env python3
"""Small dependency-free Chrome DevTools Protocol smoke-test client."""

import argparse
import base64
import json
import os
import socket
import struct
import time
import urllib.request


class WebSocket:
    def __init__(self, url):
        _, address = url.split("://", 1)
        host, path = address.split("/", 1)
        self.socket = socket.create_connection(
            (host.split(":")[0], int(host.split(":")[1])), timeout=5
        )
        self.errors = []
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET /{path} HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            "Origin: http://127.0.0.1\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.socket.sendall(request.encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            response += self.socket.recv(4096)
        if not response.startswith(b"HTTP/1.1 101"):
            raise RuntimeError(f"Chrome DevTools websocket rejected: {response!r}")

    def close(self):
        self.socket.close()

    def send(self, value):
        payload = json.dumps(value, separators=(",", ":")).encode("utf-8")
        mask = os.urandom(4)
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        size = len(masked)
        if size < 126:
            header = bytes((0x81, 0x80 | size))
        elif size < 65536:
            header = bytes((0x81, 0x80 | 126)) + struct.pack(">H", size)
        else:
            header = bytes((0x81, 0x80 | 127)) + struct.pack(">Q", size)
        self.socket.sendall(header + mask + masked)

    def receive(self):
        first, second = self.socket.recv(2)
        size = second & 0x7F
        if size == 126:
            size = struct.unpack(">H", self.socket.recv(2))[0]
        elif size == 127:
            size = struct.unpack(">Q", self.socket.recv(8))[0]
        mask = self.socket.recv(4) if second & 0x80 else None
        payload = b""
        while len(payload) < size:
            payload += self.socket.recv(size - len(payload))
        if mask:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        return first & 0x0F, payload

    def command(self, method, params, identifier):
        self.send(
            {
                "id": identifier,
                "method": method,
                "params": params,
            }
        )
        while True:
            kind, payload = self.receive()
            if kind != 1:
                continue
            response = json.loads(payload)
            method = response.get("method")
            params = response.get("params", {})
            if method == "Runtime.exceptionThrown":
                details = params.get("exceptionDetails", {})
                exception = details.get("exception", {})
                description = exception.get("description") or details.get("text") or "unknown exception"
                self.errors.append(f"uncaught runtime exception: {description}")
            elif method == "Runtime.consoleAPICalled" and params.get("type") == "error":
                arguments = params.get("args", [])
                message = " ".join(
                    str(
                        argument.get("value")
                        if argument.get("value") is not None
                        else argument.get("description", "")
                    )
                    for argument in arguments
                )
                self.errors.append("console.error: " + (message or "unspecified error"))
            if response.get("id") == identifier:
                if "error" in response:
                    raise RuntimeError(json.dumps(response))
                return response.get("result")

    def evaluate(self, expression, identifier):
        result = self.command(
            "Runtime.evaluate",
            {"expression": expression, "returnByValue": True},
            identifier,
        )
        if "exceptionDetails" in result:
            raise RuntimeError(json.dumps(result))
        return result.get("result", {}).get("value")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug-port", type=int, required=True)
    parser.add_argument("--page-url", required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--skip-text-input", action="store_true")
    parser.add_argument("--skip-resize", action="store_true")
    parser.add_argument("--skip-pointer", action="store_true")
    parser.add_argument("--skip-touch", action="store_true")
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
        websocket.command("Runtime.enable", {}, 8)
        websocket.command("Page.enable", {}, 9)
        websocket.errors.clear()
        websocket.command("Page.reload", {"ignoreCache": True}, 10)
        probe_sent = args.skip_text_input
        pointer_sent = args.skip_pointer
        touch_sent = args.skip_touch
        resize_sent = args.skip_resize
        expression = (
            "JSON.stringify({"
            "result:document.documentElement?.dataset.nativekitResult || '',"
            "status:document.getElementById('status')?.textContent || '',"
            "webgl2:!!document.getElementById('canvas')?.getContext('webgl2'),"
            "width:document.getElementById('canvas')?.width || 0,"
            "height:document.getElementById('canvas')?.height || 0"
            "})"
        )
        state = {}
        while time.monotonic() < deadline:
            if not probe_sent:
                probe_sent = bool(websocket.evaluate(
                    "(()=>{const input=document.getElementById('__nativekit_text_input');"
                    "if(!input)return false;"
                    "input.dispatchEvent(new InputEvent('input',{bubbles:true,data:'A',"
                    "inputType:'insertText'}));return true;})()",
                    2,
                ))
            if not resize_sent:
                resize_sent = bool(websocket.evaluate(
                    "(()=>{const canvas=document.getElementById('canvas');"
                    "if(!canvas)return false;canvas.style.width='640px';"
                    "canvas.style.height='480px';window.dispatchEvent(new Event('resize'));"
                    "return true;})()",
                    3,
                ))
            if not pointer_sent or not touch_sent:
                rect = websocket.evaluate(
                    "(()=>{const r=document.getElementById('canvas')?.getBoundingClientRect();"
                    "return r?{x:r.left+r.width/2,y:r.top+r.height/2}:null;})()",
                    4,
                )
                if rect:
                    if not pointer_sent:
                        websocket.command(
                            "Input.dispatchMouseEvent",
                            {"type": "mouseMoved", "x": rect["x"], "y": rect["y"]},
                            5,
                        )
                        pointer_sent = True
                    if not touch_sent:
                        point = {"x": rect["x"], "y": rect["y"], "radiusX": 1, "radiusY": 1,
                                 "force": 1, "id": 42}
                        websocket.command(
                            "Input.dispatchTouchEvent",
                            {"type": "touchStart", "touchPoints": [point]},
                            6,
                        )
                        websocket.command(
                            "Input.dispatchTouchEvent",
                            {"type": "touchEnd", "touchPoints": []},
                            7,
                        )
                        touch_sent = True
            value = websocket.evaluate(expression, 1)
            state = json.loads(value)
            if state["result"]:
                if state["result"] != "0":
                    raise RuntimeError(f"NativeKit browser smoke test failed: {state}")
                if (not probe_sent or not pointer_sent or not touch_sent or not resize_sent or
                        not state["webgl2"] or state["width"] <= 0 or state["height"] <= 0):
                    raise RuntimeError(f"NativeKit browser canvas is invalid: {state}")
                time.sleep(0.1)
                websocket.evaluate("document.documentElement.dataset.nativekitResult || ''", 11)
                if websocket.errors:
                    raise RuntimeError("browser reported errors: " + json.dumps(websocket.errors))
                print(f"web smoke passed: {state}")
                return 0
            time.sleep(0.1)
        raise RuntimeError(f"NativeKit browser smoke test timed out: {state}")
    finally:
        websocket.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"web smoke failed: {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
