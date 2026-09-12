#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
artifact_dir=${NATIVEKIT_WEB_ARTIFACT_DIR:-"$repo_dir/build-web/modules/ui"}
output=${NATIVEKIT_WEB_BENCHMARK_JSON:-"$repo_dir/out/benchmark-web-haxeon.json"}
warmup=${NATIVEKIT_WEB_BENCHMARK_WARMUP:-120}
frames=${NATIVEKIT_WEB_BENCHMARK_FRAMES:-600}
mode=${NATIVEKIT_WEB_BENCHMARK_MODE:-browser}
scenario=${NATIVEKIT_WEB_BENCHMARK_SCENARIO:-full}
profile=${NATIVEKIT_WEB_BENCHMARK_PROFILE:-0}
gpu=${NATIVEKIT_WEB_BENCHMARK_GPU:-software}
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
    echo "No supported browser found; set NK_WEB_BROWSER." >&2
    exit 1
fi

mkdir -p "$(dirname "$output")"
browser_flags=(--headless=new --no-sandbox --disable-dev-shm-usage --no-first-run)
case "$gpu" in
    software)
        runner=headless-software-webgl
        browser_flags+=(--disable-gpu --enable-unsafe-swiftshader)
        ;;
    hardware)
        runner=headless-hardware-webgl
        browser_flags+=(--enable-gpu --ignore-gpu-blocklist --disable-gpu-sandbox --use-gl=angle --use-angle=gl)
        ;;
    *)
        echo "NATIVEKIT_WEB_BENCHMARK_GPU must be software or hardware" >&2
        exit 2
        ;;
esac
http_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
debug_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
temp_dir=$(mktemp -d)
http_pid=""
browser_pid=""

cleanup() {
    local status=$?
    if [[ -n "$browser_pid" ]]; then
        kill "$browser_pid" 2>/dev/null || true
        wait "$browser_pid" 2>/dev/null || true
    fi
    if [[ -n "$http_pid" ]]; then
        kill "$http_pid" 2>/dev/null || true
        wait "$http_pid" 2>/dev/null || true
    fi
    # Chrome may briefly leave profile workers behind after its parent exits.
    # Cleanup must never replace a successful benchmark result with a trap error.
    for _ in $(seq 1 10); do
        rm -rf -- "$temp_dir" 2>/dev/null && break
        sleep 0.1
    done
    return "$status"
}
trap cleanup EXIT

python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$artifact_dir" >"$temp_dir/http.log" 2>&1 &
http_pid=$!
page_url="http://127.0.0.1:${http_port}/nativekit_ui_haxeon.html?benchmark&runner=${runner}&mode=${mode}&scenario=${scenario}&warmup=${warmup}&frames=${frames}"
if [[ "$profile" == "1" ]]; then
    page_url+="&profile"
fi
"$browser" "${browser_flags[@]}" --user-data-dir="$temp_dir/profile" \
    --remote-debugging-port="$debug_port" --remote-allow-origins='*' \
    "$page_url" >"$temp_dir/browser.log" 2>&1 &
browser_pid=$!

for _ in $(seq 1 100); do
    if curl --silent --fail "http://127.0.0.1:${debug_port}/json/version" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

python3 -B "$repo_dir/tools/web_benchmark.py" --debug-port "$debug_port" \
    --page-url "$page_url" --output "$output" --timeout 90
