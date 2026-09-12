#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
artifact_dir=${NATIVEKIT_WEB_ARTIFACT_DIR:-"$repo_dir/build-web/modules/ui"}
browser=${NK_WEB_BROWSER:-$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)}
update=false
if [[ ${1:-} == "--update" ]]; then
    update=true
elif [[ $# -ne 0 ]]; then
    echo "usage: tools/test-web-visual.sh [--update]" >&2
    exit 2
fi
if [[ ! -f "$artifact_dir/nativekit_ui_haxeon.html" ]]; then
    echo "Haxeon web artifact is missing. Run tools/build-web.sh first." >&2
    exit 1
fi
if [[ -z "$browser" ]]; then
    echo "No supported headless browser found; set NK_WEB_BROWSER." >&2
    exit 1
fi

cases=(
    "900x650|900x650|||16,1,2"
    "700x800|700x800|||16,1,2"
    "1400x900|1400x900|||16,1,2"
    "caret-latin|900x650||290,420|3,1,1"
    "caret-arabic|900x650||445,420|12,2,2"
    "caret-hebrew|900x650||375,420|21,2,2"
    "caret-cjk|900x650||520,420|29,1,1"
    "caret-emoji|900x650||575,420|33,1,1"
    "light-theme|900x650||100,468|16,1,2"
    "animated|900x650|time=2250||16,1,2"
)

for visual_case in "${cases[@]}"; do
    IFS='|' read -r case_name size extra_query click caret <<<"$visual_case"
    width=${size%x*}
    height=${size#*x}
    http_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
    debug_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
    temp_dir=$(mktemp -d)
    http_pid=""
    browser_pid=""
    cleanup_case() {
        [[ -z "$browser_pid" ]] || kill "$browser_pid" 2>/dev/null || true
        pkill -TERM -f -- "--user-data-dir=$temp_dir/profile" 2>/dev/null || true
        [[ -z "$http_pid" ]] || kill "$http_pid" 2>/dev/null || true
        [[ -z "$browser_pid" ]] || wait "$browser_pid" 2>/dev/null || true
        [[ -z "$http_pid" ]] || wait "$http_pid" 2>/dev/null || true
        for _ in 1 2 3; do
            rm -rf "$temp_dir" 2>/dev/null && break
            sleep 0.1
        done
    }
    trap cleanup_case EXIT
    python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$artifact_dir" \
        >"$temp_dir/http.log" 2>&1 &
    http_pid=$!
    page_url="http://127.0.0.1:${http_port}/nativekit_ui_haxeon.html?visual&width=${width}&height=${height}"
    [[ -z "$extra_query" ]] || page_url+="&$extra_query"
    "$browser" --headless=new --no-sandbox --disable-dev-shm-usage --disable-gpu \
        --enable-unsafe-swiftshader --force-device-scale-factor=1 --no-first-run \
        --window-size="$width,$height" --user-data-dir="$temp_dir/profile" \
        --remote-debugging-port="$debug_port" --remote-allow-origins='*' \
        "$page_url" >"$temp_dir/browser.log" 2>&1 &
    browser_pid=$!
    for _ in $(seq 1 100); do
        if curl --silent --fail "http://127.0.0.1:${debug_port}/json/version" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
    arguments=(--debug-port "$debug_port" --page-url "$page_url" --width "$width" --height "$height"
        --reference "$repo_dir/modules/ui/tests/golden/showcase-${case_name}.png"
        --artifact-dir "$repo_dir/build-web/visual-diffs")
    [[ -z "$click" ]] || arguments+=(--click "$click")
    IFS=',' read -r caret_offset caret_affinity caret_direction <<<"$caret"
    arguments+=(--expect-offset "$caret_offset" --expect-affinity "$caret_affinity"
        --expect-direction "$caret_direction")
    [[ "$update" == false ]] || arguments+=(--update)
    python3 "$repo_dir/tools/web_visual.py" "${arguments[@]}"
    cleanup_case
    trap - EXIT
done
