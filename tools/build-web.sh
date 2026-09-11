#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
emsdk_dir="$repo_dir/.tools/emsdk"
build_dir=${NATIVEKIT_WEB_BUILD_DIR:-"$repo_dir/build-web"}
build_type=${CMAKE_BUILD_TYPE:-Release}

if [[ ! -f "$emsdk_dir/emsdk_env.sh" ]]; then
    echo "Emscripten is not installed. Run tools/setup-web.sh first." >&2
    exit 1
fi
source "$emsdk_dir/emsdk_env.sh" >/dev/null

if [[ -n "${NK_SOKOL_SHDC:-}" ]]; then
    shdc="$NK_SOKOL_SHDC"
else
    case "$(uname -s):$(uname -m)" in
        Linux:x86_64) shdc="$repo_dir/.tools/sokol-tools-bin/bin/linux/sokol-shdc" ;;
        Linux:aarch64|Linux:arm64) shdc="$repo_dir/.tools/sokol-tools-bin/bin/linux_arm64/sokol-shdc" ;;
        Darwin:x86_64) shdc="$repo_dir/.tools/sokol-tools-bin/bin/osx/sokol-shdc" ;;
        Darwin:arm64) shdc="$repo_dir/.tools/sokol-tools-bin/bin/osx_arm64/sokol-shdc" ;;
        *) shdc="" ;;
    esac
    if [[ ! -x "$shdc" ]]; then
        shdc=$(command -v sokol-shdc || true)
    fi
fi

if [[ -z "$shdc" || ! -x "$shdc" ]]; then
    echo "sokol-shdc is not installed. Run tools/setup-web.sh or set NK_SOKOL_SHDC." >&2
    exit 1
fi

emcmake cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNK_BUILD_SHARED=OFF \
    -DNK_BUILD_TESTS=OFF \
    -DNK_BUILD_EXAMPLES=ON \
    -DNK_BUILD_SOKOL=OFF \
    -DNK_BUILD_UI=ON \
    -DNK_SOKOL_BACKEND=gles3 \
    -DNK_SOKOL_SHDC="$shdc"
cmake --build "$build_dir" --target nativekit_ui_c_api

artifact_dir="$build_dir/modules/ui"
echo
echo "Web build complete:"
echo "  $artifact_dir/nativekit_ui_c_api.html"
echo
echo "Serve it over HTTP (required for the packaged font):"
echo "  python3 -m http.server --directory \"$artifact_dir\" 8080"
