#!/usr/bin/env python3
"""Run the WebSocket message contract in Chrome (requires Python websockets)."""
import asyncio
import functools
import http.server
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import urllib.request

import websockets


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


async def main():
    build = pathlib.Path(sys.argv[1]).resolve()
    assert (build / 'tests/nativekit_web_transport_messages.html').is_file()
    seen = []

    async def echo(connection):
        try:
            async for value in connection:
                assert isinstance(value, str), 'TEXT sends must use text frames'
                seen.append(value)
                await connection.ping(str(len(seen)).encode())
                await connection.send([value[:2], value[2:]] if value else '')
        except websockets.exceptions.ConnectionClosedError:
            # NativeKit resource disposal may close TCP without a WebSocket close exchange.
            if len(seen) != 3: raise

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass

    http_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=str(build)))
    threading.Thread(target=http_server.serve_forever, daemon=True).start()
    try:
        async with websockets.serve(echo, '127.0.0.1', 0) as server:
            port = server.sockets[0].getsockname()[1]
            debug = free_port()
            chrome = os.environ.get('CHROME_BIN') or shutil.which('google-chrome') or shutil.which('chromium')
            if not chrome:
                raise RuntimeError('Chrome or Chromium is required')
            with tempfile.TemporaryDirectory(prefix='nativekit-web-transport-') as profile:
                process = subprocess.Popen([chrome, '--headless', '--no-sandbox', '--disable-gpu',
                    '--remote-allow-origins=*', f'--remote-debugging-port={debug}', f'--user-data-dir={profile}',
                    'about:blank'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    targets = None
                    for _ in range(100):
                        try:
                            with urllib.request.urlopen(f'http://127.0.0.1:{debug}/json', timeout=.2) as response:
                                targets = json.load(response)
                            break
                        except OSError:
                            await asyncio.sleep(.05)
                    if not targets:
                        raise RuntimeError('Chrome did not expose its debug endpoint')
                    target = next(t for t in targets if t['type'] == 'page')
                    async with websockets.connect(target['webSocketDebuggerUrl']) as cdp:
                        sequence = 0

                        async def call(method, params):
                            nonlocal sequence
                            sequence += 1
                            await cdp.send(json.dumps({'id': sequence, 'method': method, 'params': params}))
                            while True:
                                result = json.loads(await asyncio.wait_for(cdp.recv(), 5))
                                if result.get('id') == sequence:
                                    if 'error' in result:
                                        raise RuntimeError(result['error'])
                                    return result.get('result', {})

                        url = f'http://127.0.0.1:{http_server.server_port}/tests/nativekit_web_transport_messages.html?port={port}'
                        await call('Page.navigate', {'url': url})
                        for _ in range(150):
                            result = await call('Runtime.evaluate', {'expression': 'document.documentElement.dataset.nativekitMessages'})
                            if result.get('result', {}).get('value') == 'passed':
                                assert seen == ['hello', '', 'world'], seen
                                print('Browser WebSocket text, empty messages, fragmented replies and atomic receives passed')
                                return
                            await asyncio.sleep(.1)
                        raise RuntimeError(f'Browser contract did not complete; server saw {seen!r}')
                finally:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    finally:
        http_server.shutdown()
        http_server.server_close()


if __name__ == '__main__':
    asyncio.run(main())
