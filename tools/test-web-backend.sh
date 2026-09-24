#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$repo_dir/build-web"}
if [[ "$build_dir" != /* ]]; then
    build_dir="$PWD/$build_dir"
fi

for artifact in \
    nativekit_platform_parity.html \
    nativekit_web_accessibility.html \
    nativekit_web_system_equivalents.html \
    nativekit_web_tasks.html; do
    if [[ ! -f "$build_dir/tests/$artifact" ]]; then
        echo "test-web-backend: missing browser artifact: $build_dir/tests/$artifact" >&2
        exit 2
    fi
done

if [[ -n "${CHROME_BIN:-}" ]]; then
    chrome_bin=$CHROME_BIN
elif command -v google-chrome >/dev/null 2>&1; then
    chrome_bin=$(command -v google-chrome)
elif command -v chromium >/dev/null 2>&1; then
    chrome_bin=$(command -v chromium)
elif command -v chromium-browser >/dev/null 2>&1; then
    chrome_bin=$(command -v chromium-browser)
else
    echo "test-web-backend: Chrome or Chromium is required" >&2
    exit 2
fi

read -r http_port debug_port < <(python3 -c '
import socket
sockets = []
try:
    for _ in range(2):
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    print(*(sock.getsockname()[1] for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
')

temp_dir=$(mktemp -d)
server_pid=
browser_pid=
cleanup() {
    if [[ -n "$browser_pid" ]]; then
        kill "$browser_pid" 2>/dev/null || true
        wait "$browser_pid" 2>/dev/null || true
    fi
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf -- "$temp_dir"
}
trap cleanup EXIT

python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$build_dir" \
    >"$temp_dir/http.log" 2>&1 &
server_pid=$!
base_url="http://127.0.0.1:$http_port"
page_url="$base_url/tests/nativekit_platform_parity.html"

for attempt in $(seq 1 100); do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        cat "$temp_dir/http.log" >&2
        echo "test-web-backend: HTTP server exited during startup" >&2
        exit 1
    fi
    if curl --fail --silent "$page_url" >/dev/null; then
        break
    fi
    sleep 0.1
done
if ! curl --fail --silent "$page_url" >/dev/null; then
    cat "$temp_dir/http.log" >&2
    echo "test-web-backend: HTTP server did not become ready" >&2
    exit 1
fi

"$chrome_bin" \
    --headless=new \
    --no-sandbox \
    --disable-dev-shm-usage \
    --disable-background-networking \
    --disable-extensions \
    --no-first-run \
    --no-default-browser-check \
    --remote-allow-origins='*' \
    --remote-debugging-address=127.0.0.1 \
    --remote-debugging-port="$debug_port" \
    --user-data-dir="$temp_dir/chrome-profile" \
    --enable-webgl \
    --ignore-gpu-blocklist \
    --enable-unsafe-swiftshader \
    --use-gl=angle \
    --use-angle=swiftshader \
    "$page_url" >"$temp_dir/chrome.log" 2>&1 &
browser_pid=$!

PYTHONDONTWRITEBYTECODE=1 python3 "$repo_dir/tools/web_dataset_smoke.py" \
    --debug-port "$debug_port" \
    --page-url "$page_url" \
    --timeout 90 \
    --test-page "$page_url" \
    --dataset-key nativekitPlatformParity \
    --test-page "$base_url/tests/nativekit_web_accessibility.html" \
    --dataset-key nativekitAccessibilityResult \
    --test-page "$base_url/tests/nativekit_web_system_equivalents.html" \
    --dataset-key nativekitSystemResult \
    --test-page "$base_url/tests/nativekit_web_tasks.html" \
    --dataset-key nativekitTaskResult
