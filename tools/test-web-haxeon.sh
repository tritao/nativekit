#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
artifact_dir=${NATIVEKIT_WEB_ARTIFACT_DIR:-"$repo_dir/build-web/modules/ui"}
browser=${NK_WEB_BROWSER:-}

if [[ ! -f "$artifact_dir/nativekit_ui_haxeon.html" ]]; then
    echo "Haxeon web artifact is missing. Run tools/build-web.sh first." >&2
    exit 1
fi
if [[ -z "$browser" ]]; then
    for candidate in google-chrome chromium chromium-browser; do
        if command -v "$candidate" >/dev/null 2>&1; then
            browser=$(command -v "$candidate")
            break
        fi
    done
fi
if [[ -z "$browser" ]]; then
    echo "No supported headless browser found; set NK_WEB_BROWSER." >&2
    exit 1
fi

http_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
debug_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
temp_dir=$(mktemp -d)
http_pid=""
browser_pid=""

cleanup() {
    if [[ -n "$browser_pid" ]]; then
        kill "$browser_pid" 2>/dev/null || true
        wait "$browser_pid" 2>/dev/null || true
    fi
    if [[ -n "$http_pid" ]]; then
        kill "$http_pid" 2>/dev/null || true
        wait "$http_pid" 2>/dev/null || true
    fi
    rmdir "$temp_dir" 2>/dev/null || true
}
trap cleanup EXIT

python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$artifact_dir" \
    >"$temp_dir/http.log" 2>&1 &
http_pid=$!

page_url="http://127.0.0.1:${http_port}/nativekit_ui_haxeon.html?smoke"
"$browser" --headless=new --no-sandbox --disable-dev-shm-usage --disable-gpu \
    --enable-unsafe-swiftshader --no-first-run --user-data-dir="$temp_dir/profile" \
    --remote-debugging-port="$debug_port" --remote-allow-origins='*' \
    "$page_url" >"$temp_dir/browser.log" 2>&1 &
browser_pid=$!

for _ in $(seq 1 100); do
    if curl --silent --fail "http://127.0.0.1:${debug_port}/json/version" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

python3 "$repo_dir/tools/web_smoke.py" --debug-port "$debug_port" \
    --page-url "$page_url" --skip-text-input
