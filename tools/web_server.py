#!/usr/bin/env python3
"""Small static server used by browser tests, with optional COOP/COEP headers."""

import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class Handler(SimpleHTTPRequestHandler):
    cross_origin_isolated = False

    def end_headers(self):
        if self.cross_origin_isolated:
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cross-origin-isolated", action="store_true")
    args = parser.parse_args()
    Handler.cross_origin_isolated = args.cross_origin_isolated
    handler = functools.partial(Handler, directory=args.directory)
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        server.serve_forever()


if __name__ == "__main__":
    main()
