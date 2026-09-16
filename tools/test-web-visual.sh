#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${NATIVEKIT_WEB_BUILD_DIR:-"$repo_dir/build-web"}
artifact_dir=${NATIVEKIT_WEB_ARTIFACT_DIR:-"$build_dir/modules/ui"}
browser=${NK_WEB_BROWSER:-$(command -v google-chrome || command -v chromium || command -v chromium-browser || true)}
update=false
ui_only=false
case_filter=""
gallery_dir=""
while (($#)); do
    case "$1" in
        --update) update=true ;;
        --ui-only) ui_only=true ;;
        --case)
            shift
            if (($# == 0)); then
                echo "usage: tools/test-web-visual.sh [--update] [--ui-only] [--case NAME]" >&2
                exit 2
            fi
            case_filter=$1
            ;;
        --ui-gallery)
            shift
            if (($# == 0)); then
                echo "usage: tools/test-web-visual.sh [--update] [--ui-only] [--case NAME] [--ui-gallery DIR]" >&2
                exit 2
            fi
            gallery_dir=$1
            ui_only=true
            ;;
        *)
            echo "usage: tools/test-web-visual.sh [--update] [--ui-only] [--case NAME] [--ui-gallery DIR]" >&2
            exit 2
            ;;
    esac
    shift
done
if [[ ! -f "$artifact_dir/nativekit_ui_haxeon.html" ]]; then
    echo "Haxeon web artifact is missing. Run tools/build-web.sh first." >&2
    exit 1
fi
host_wasm_artifact="$artifact_dir/nativekit_ui_haxeon.wasm"
guest_wasm_artifact="$artifact_dir/nativekit_ui_haxeon_guest.wasm"
if [[ ! -f "$host_wasm_artifact" || ! -f "$guest_wasm_artifact" ]]; then
    echo "Haxeon WebAssembly artifact is missing. Run tools/build-web.sh first." >&2
    exit 1
fi
guest_source_artifact="$build_dir/modules/ui/nativekit_ui_showcase_wasm32.wasm"
guest_build_artifact="$guest_wasm_artifact"
[[ ! -f "$guest_source_artifact" ]] || guest_build_artifact="$guest_source_artifact"
stale_native_source=$(find "$repo_dir/src" "$repo_dir/include" "$repo_dir/modules/ui" \
    "$repo_dir/cmake" "$repo_dir/vendor" \
    \( -type d \( -name .git -o -name build -o -name build-web -o -name build-ui \
        -o -name .tools \) -prune \) -o \
    \( -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \
        -o -name '*.h' -o -name '*.hpp' -o -name '*.cmake' -o -name '*.glsl' \) \
        -newer "$host_wasm_artifact" -print -quit \))
haxeon_dir=${HAXEON_DIR:-}
if [[ -z "$haxeon_dir" && -f "$build_dir/build.ninja" ]]; then
    haxeon_dir=$(sed -n 's/.*HAXEON_DIR=\([^[:space:]]*\).*/\1/p' \
        "$build_dir/build.ninja" | head -n 1)
fi
haxeon_dir=${haxeon_dir:-"$repo_dir/../realtime-haxe"}
haxe_source_dirs=(
    "$repo_dir/modules/ui/haxe"
    "$repo_dir/modules/ui/bindings"
    "$repo_dir/modules/ui/examples/ui_showcase"
    "$repo_dir/bindings"
)
[[ ! -d "$haxeon_dir/src" ]] || haxe_source_dirs+=("$haxeon_dir/src")
stale_haxe_source=$(find "${haxe_source_dirs[@]}" -type f \
    -name '*.hx' -newer "$guest_build_artifact" -print -quit)
for guest_input in "$repo_dir/modules/ui/tools/showcase-wasm.sh" \
    "$repo_dir/modules/ui/cmake/wasm_memory_contract.json.in"; do
    if [[ -f "$guest_input" && "$guest_input" -nt "$guest_build_artifact" && \
        -z "$stale_haxe_source" ]]; then
        stale_haxe_source="$guest_input"
    fi
done
if [[ -n "$stale_native_source" || -n "$stale_haxe_source" ]]; then
    stale_source=${stale_native_source:-$stale_haxe_source}
    echo "Web visual artifact is older than source input: ${stale_source#"$repo_dir"/}" >&2
    echo "Rebuild it with tools/build-web.sh before running visual tests." >&2
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
    "zoom-110|900x650|||16,1,2|1.1"
    "ui-overview|1200x800|uiVisual=0||"
    "ui-controls|1200x800|uiVisual=1||"
    "ui-layout|1200x800|uiVisual=5||"
    "ui-gestures|1200x800|uiVisual=11||"
    "ui-graphics|1200x800|uiVisual=15||"
    "ui-controls-light|1200x800|uiVisual=2||"
    "ui-controls-focused|1200x800|uiVisual=3||"
    "ui-text-focused|1200x800|uiVisual=4||"
    "ui-lists-scrolled|1200x800|uiVisual=6||"
    "ui-dialog|1200x800|uiVisual=7||"
    "ui-popup|1200x800|uiVisual=8||"
    "ui-menu|1200x800|uiVisual=9||"
    "ui-controls-compact|700x800|uiVisual=10||"
    "ui-inspector-interactive|1200x800|uiVisual=1|650,218|"
    "ui-lists-light|1200x800|uiVisual=12||"
    "ui-textarea-selection|1200x800|uiVisual=13||"
    "ui-text-composition|1200x800|uiVisual=23||"
    "ui-menu-light|1200x800|uiVisual=14||"
    "ui-overview-compact|700x800|uiVisual=16||"
    "ui-text-compact|700x800|uiVisual=17||"
    "ui-layout-compact|700x800|uiVisual=18||"
    "ui-lists-compact|700x800|uiVisual=19||"
    "ui-overlays-compact|700x800|uiVisual=20||"
    "ui-gestures-compact|700x800|uiVisual=21||"
    "ui-graphics-compact|700x800|uiVisual=22||"
    "ui-overview-zoom-110|1200x800|uiVisual=0|||1.1"
)

if [[ -n "$gallery_dir" ]]; then
    cases=(
        "ui-overview|1200x800|uiVisual=0||"
        "ui-controls|1200x800|uiVisual=1||"
        "ui-text|1200x800|uiVisual=24||"
        "ui-layout|1200x800|uiVisual=5||"
        "ui-lists|1200x800|uiVisual=25||"
        "ui-overlays|1200x800|uiVisual=26||"
        "ui-gestures|1200x800|uiVisual=11||"
        "ui-graphics|1200x800|uiVisual=15||"
        "ui-graphics-paths|1200x800|uiVisual=27||"
        "ui-graphics-text|1200x800|uiVisual=28||"
        "ui-graphics-images|1200x800|uiVisual=29||"
        "ui-graphics-rendering|1200x800|uiVisual=30||"
    )
fi

if [[ -n "$case_filter" ]]; then
    case_found=false
    for visual_case in "${cases[@]}"; do
        IFS='|' read -r case_name _ <<<"$visual_case"
        if [[ "$case_name" == "$case_filter" ]]; then
            case_found=true
            break
        fi
    done
    if [[ "$case_found" == false ]]; then
        echo "Unknown visual case: $case_filter" >&2
        exit 2
    fi
    if [[ "$ui_only" == true && "$case_filter" != ui-* ]]; then
        echo "Visual case is not a UI Explorer case: $case_filter" >&2
        exit 2
    fi
fi

http_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
temp_dir=$(mktemp -d)
http_pid=""
browser_pid=""
cleanup_suite() {
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
trap cleanup_suite EXIT
python3 -m http.server "$http_port" --bind 127.0.0.1 --directory "$artifact_dir" \
    >"$temp_dir/http.log" 2>&1 &
http_pid=$!

for visual_case in "${cases[@]}"; do
    IFS='|' read -r case_name size extra_query click caret device_scale <<<"$visual_case"
    if [[ -n "$case_filter" && "$case_name" != "$case_filter" ]]; then
        continue
    fi
    if [[ "$ui_only" == true && "$case_name" != ui-* ]]; then
        continue
    fi
    if [[ -n "$gallery_dir" ]]; then
        case "$case_name" in
			ui-overview|ui-controls|ui-text|ui-layout|ui-lists|ui-overlays|ui-gestures|ui-graphics|ui-graphics-paths|ui-graphics-text|ui-graphics-images|ui-graphics-rendering) ;;
            *) continue ;;
        esac
    fi
    device_scale=${device_scale:-1}
    width=${size%x*}
    height=${size#*x}
    page_url="http://127.0.0.1:${http_port}/nativekit_ui_haxeon.html?visual&visualCase=${case_name}&width=${width}&height=${height}"
    [[ -z "$extra_query" ]] || page_url+="&$extra_query"
    debug_port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
    "$browser" --headless=new --no-sandbox --disable-dev-shm-usage --disable-gpu \
        --enable-unsafe-swiftshader --no-first-run --window-size="$width,$height" \
        --user-data-dir="$temp_dir/profile-$case_name" --remote-debugging-port="$debug_port" \
        --remote-allow-origins='*' "$page_url" >"$temp_dir/browser-$case_name.log" 2>&1 &
    browser_pid=$!
    for _ in $(seq 1 100); do
        if curl --silent --fail "http://127.0.0.1:${debug_port}/json/version" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
    arguments=(--debug-port "$debug_port" --page-url "$page_url" --width "$width" --height "$height"
        --scale "$device_scale"
        --reference "$repo_dir/modules/ui/tests/golden/showcase-${case_name}.png"
        --artifact-dir "$repo_dir/build-web/visual-diffs")
    if [[ "$device_scale" != 1 ]]; then
        arguments+=(--apply-device-scale)
    fi
    if [[ "$case_name" == ui-* ]]; then
        arguments+=(--move 1,1)
    fi
    if [[ -n "$gallery_dir" ]]; then
        arguments+=(--output "$gallery_dir/${case_name#ui-}.png")
    fi
    [[ -z "$click" ]] || arguments+=(--click "$click")
    if [[ -n "$caret" ]]; then
        IFS=',' read -r caret_offset caret_affinity caret_direction <<<"$caret"
        arguments+=(--expect-offset "$caret_offset" --expect-affinity "$caret_affinity"
            --expect-direction "$caret_direction")
    fi
    [[ "$update" == false ]] || arguments+=(--update)
    python3 "$repo_dir/tools/web_visual.py" "${arguments[@]}"
    kill "$browser_pid" 2>/dev/null || true
    wait "$browser_pid" 2>/dev/null || true
    browser_pid=""
done
