#!/usr/bin/env python3
"""Loopback HTTPS->HTTP redirect contract for the libcurl HTTP backend (standard library only)."""
import http.server
import pathlib
import ssl
import subprocess
import sys
import tempfile
import threading


def handler(redirect_target):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/redirect' and redirect_target:
                self.send_response(302)
                self.send_header('Location', redirect_target)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            body = b'target'
            self.send_response(200 if self.path == '/target' else 404)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *arguments):
            pass

    return Handler


def serve(server):
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return thread


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='nativekit-http-') as temp:
        cert, key = pathlib.Path(temp) / 'ca.pem', pathlib.Path(temp) / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
            '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
            '-keyout', str(key), '-out', str(cert)], check=True, capture_output=True)
        plain = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler(None))
        http_port = plain.server_address[1]
        secure = http.server.ThreadingHTTPServer(
            ('127.0.0.1', 0), handler(f'http://127.0.0.1:{http_port}/target'))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        secure.socket = context.wrap_socket(secure.socket, server_side=True)
        https_port = secure.server_address[1]
        serve(plain)
        serve(secure)
        try:
            for mode in ('strings', 'bitmask'):
                subprocess.run([executable, str(https_port), str(http_port), str(cert), mode],
                               check=True, timeout=60)
        finally:
            plain.shutdown()
            secure.shutdown()


if __name__ == '__main__':
    main()
