#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
artifact_dir=${NATIVEKIT_WEB_ARTIFACT_DIR:-"$repo_dir/build-web/modules/ui"}
build_dir=${NATIVEKIT_WEB_BUILD_DIR:-"$repo_dir/build-web"}
browser=${NK_WEB_BROWSER:-}
frame_only=${NATIVEKIT_WEB_FRAME_ONLY:-OFF}
cross_origin_isolated=${NATIVEKIT_WEB_CROSS_ORIGIN_ISOLATED:-OFF}

if [[ ! -f "$artifact_dir/nativekit_ui_c_api.html" ]]; then
    echo "Web artifact is missing. Run tools/build-web.sh first." >&2
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

server_args=(python3 "$repo_dir/tools/web_server.py" --port "$http_port" --directory "$build_dir")
if [[ "$cross_origin_isolated" == "ON" || "$cross_origin_isolated" == "1" ]]; then
    server_args+=(--cross-origin-isolated)
fi
"${server_args[@]}" >"$temp_dir/http.log" 2>&1 &
http_pid=$!

artifact_rel=$(realpath --relative-to="$build_dir" "$artifact_dir")
if [[ "$frame_only" == "ON" || "$frame_only" == "1" ]]; then
    page_url="http://127.0.0.1:${http_port}/tests/nativekit_web_frame_backend.html?smoke"
else
    page_url="http://127.0.0.1:${http_port}/${artifact_rel}/nativekit_ui_c_api.html?smoke"
fi
"$browser" --headless=new --no-sandbox --disable-dev-shm-usage --disable-gpu \
    --enable-unsafe-swiftshader --no-first-run --user-data-dir="$temp_dir/profile" \
    --remote-debugging-port="$debug_port" --remote-allow-origins='*' \
    "$page_url" >"$temp_dir/browser.log" 2>&1 &
browser_pid=$!

debug_ready=0
for _ in $(seq 1 300); do
    if curl --silent --fail "http://127.0.0.1:${debug_port}/json/version" >/dev/null 2>&1; then
        debug_ready=1
        break
    fi
    if ! kill -0 "$browser_pid" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if [[ "$debug_ready" != 1 ]]; then
    echo "Chrome DevTools endpoint did not become ready" >&2
    cat "$temp_dir/browser.log" >&2 || true
    cat "$temp_dir/http.log" >&2 || true
    exit 1
fi

if [[ "$frame_only" != "ON" && "$frame_only" != "1" ]]; then
    if ! python3 "$repo_dir/tools/web_smoke.py" --debug-port "$debug_port" --page-url "$page_url"; then
        cat "$temp_dir/browser.log" >&2 || true
        cat "$temp_dir/http.log" >&2 || true
        exit 1
    fi
fi

for test_artifact in \
    "$build_dir/tests/nativekit_platform_parity.html" \
    "$build_dir/tests/nativekit_web_frame_backend.html" \
    "$build_dir/tests/nativekit_web_accessibility.html" \
    "$build_dir/tests/nativekit_web_system_equivalents.html"; do
    if [[ ! -f "$test_artifact" ]]; then
        echo "Web integration artifact is missing: $test_artifact" >&2
        exit 1
    fi
done

if [[ "$frame_only" == "ON" || "$frame_only" == "1" ]]; then
    python3 "$repo_dir/tools/web_dataset_smoke.py" \
        --debug-port "$debug_port" --page-url "$page_url" \
        --test-page "$page_url" \
        --dataset-key nativekitFrameBackendResult
else
    python3 "$repo_dir/tools/web_dataset_smoke.py" \
        --debug-port "$debug_port" --page-url "$page_url" \
        --test-page "http://127.0.0.1:${http_port}/tests/nativekit_platform_parity.html" \
        --dataset-key nativekitPlatformParity \
        --test-page "http://127.0.0.1:${http_port}/tests/nativekit_web_frame_backend.html" \
        --dataset-key nativekitFrameBackendResult \
        --test-page "http://127.0.0.1:${http_port}/tests/nativekit_web_accessibility.html" \
        --dataset-key nativekitAccessibilityResult \
        --test-page "http://127.0.0.1:${http_port}/tests/nativekit_web_system_equivalents.html" \
        --dataset-key nativekitSystemResult \
        --test-page "http://127.0.0.1:${http_port}/tests/nativekit_web_system_equivalents.html?orientation-smoke" \
        --dataset-key nativekitSystemResult
fi
