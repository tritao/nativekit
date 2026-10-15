#!/usr/bin/env python3
"""Loopback WSS contract (Python websockets, OpenSSL; no changes to system trust)."""
import asyncio
import os
import pathlib
import ssl
import subprocess
import sys
import tempfile

import websockets


async def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='nativekit-tls-') as temp:
        cert, key = pathlib.Path(temp) / 'ca.pem', pathlib.Path(temp) / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
            '-keyout', str(key), '-out', str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        seen = []

        async def echo(connection):
            try:
                async for value in connection:
                    assert isinstance(value, str), 'WSS sends must use TEXT frames'
                    seen.append(value)
                    await connection.ping(str(len(seen)).encode())
                    await connection.send([value[i:i+1000] for i in range(0, len(value), 1000)] if value else '')
            except websockets.exceptions.ConnectionClosedError:
                # NativeKit resource disposal may close TCP without a WebSocket close exchange.
                if len(seen) != 3: raise

        async with websockets.serve(echo, '127.0.0.1', 0, ssl=context) as server:
            port = server.sockets[0].getsockname()[1]
            env = os.environ.copy()
            env['SSL_CERT_FILE'] = str(cert)
            process = await asyncio.create_subprocess_exec(str(executable), str(port), env=env)
            try:
                code = await asyncio.wait_for(process.wait(), 15)
                assert code == 0, f'TLS client exited {code}'
                assert seen == ['x' * 20000, '', 'world'], [len(v) for v in seen]
                print('WSS text, empty messages, multi-callback fragmented replies and ping handling passed')
            finally:
                if process.returncode is None:
                    process.kill()
                    await process.wait()


if __name__ == '__main__':
    asyncio.run(main())
