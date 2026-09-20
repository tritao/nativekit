#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
emsdk_dir="$repo_dir/.tools/emsdk"
build_dir=${NATIVEKIT_WEB_BUILD_DIR:-"$repo_dir/build-web"}
build_type=${CMAKE_BUILD_TYPE:-Release}
memory_stats=${NKUI_HAXEON_MEMORY_STATS:-OFF}
haxeon_target=${NKUI_HAXEON_TARGET:-wasm32}
exception_mode=${NATIVEKIT_HAXEON_EXCEPTION_MODE:-legacy}
bundle_fonts=${NKUI_HAXEON_BUNDLE_FONTS:-OFF}
subset_fonts=${NKUI_HAXEON_SUBSET_FONTS:-ON}
threaded_render=${NATIVEKIT_WEB_THREADED_RENDER:-OFF}

if [[ ! -f "$emsdk_dir/emsdk_env.sh" ]]; then
    echo "Emscripten is not installed. Run tools/setup-web.sh first." >&2
    exit 1
fi
source "$emsdk_dir/emsdk_env.sh" >/dev/null

emcmake cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNK_BUILD_SHARED=OFF \
    -DNK_BUILD_TESTS=ON \
    -DNK_BUILD_EXAMPLES=ON \
    -DNK_BUILD_GPU=OFF \
    -DNK_BUILD_UI=ON \
    -DNKUI_HAXEON_TARGET="$haxeon_target" \
    -DNKUI_HAXEON_EXCEPTION_MODE="$exception_mode" \
    -DNKUI_HAXEON_MEMORY_STATS="$memory_stats" \
    -DNKUI_HAXEON_BUNDLE_FONTS="$bundle_fonts" \
    -DNKUI_HAXEON_SUBSET_FONTS="$subset_fonts" \
    -DNK_WEB_THREADED_RENDER="$threaded_render" \
    -DNK_SOKOL_BACKEND=gles3

web_targets=(
    nativekit_ui_c_api
    nativekit_platform_parity
    nativekit_web_accessibility
    nativekit_web_system_equivalents
)
haxeon_available=0
target_help=$(cmake --build "$build_dir" --target help)
if grep -Eq '(^|[[:space:]])nativekit_ui_haxeon(:|[[:space:]]|$)' <<<"$target_help"; then
    web_targets+=(nativekit_ui_haxeon)
    haxeon_available=1
else
    echo "Haxeon UI target is unavailable; skipping the optional Haxeon Web artifact." >&2
fi
cmake --build "$build_dir" --target "${web_targets[@]}"

artifact_dir="$build_dir/modules/ui"
echo
echo "Web build complete:"
echo "  $artifact_dir/nativekit_ui_c_api.html"
if [[ "$haxeon_available" == 1 ]]; then
    echo "  $artifact_dir/nativekit_ui_haxeon.html"
else
    echo "  Haxeon UI artifact: skipped"
fi
echo "  $build_dir/tests/nativekit_platform_parity.html"
echo "  $build_dir/tests/nativekit_web_accessibility.html"
echo "  $build_dir/tests/nativekit_web_system_equivalents.html"
echo
if [[ "$haxeon_available" == 1 && ("$bundle_fonts" == "ON" || "$bundle_fonts" == "1") ]]; then
    echo "Serve it over HTTP (fonts are bundled in nativekit_ui_haxeon.data; browser tests use the build root):"
else
    echo "Serve it over HTTP (required for the external font assets and browser tests):"
fi
echo "  python3 -m http.server --directory \"$build_dir\" 8080"
