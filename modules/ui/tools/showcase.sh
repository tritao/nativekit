#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$repo_dir/build-ui"}
artifact="$build_dir/nativekit_ui_showcase.hl"
build_only=false
program_args=()

for arg in "$@"; do
    if [[ "$arg" == "--build-only" ]]; then
        build_only=true
    else
        program_args+=("$arg")
    fi
done

if [[ ! -x "$haxeon_dir/.tools/haxe/haxe" ]]; then
    echo "showcase: Haxeon toolchain not found at $haxeon_dir" >&2
    echo "showcase: set HAXEON_DIR to the realtime-haxe checkout" >&2
    exit 1
fi

cmake --build "$build_dir" --target nativekit_ui
"$module_dir/tools/check-hxi.sh"
if [[ -x "$haxeon_dir/scripts/build-runtime.sh" ]]; then
    "$haxeon_dir/scripts/build-runtime.sh"
elif [[ ! -x "$haxeon_dir/.tools/hashlink/hl" && -x "$haxeon_dir/scripts/build-native.sh" ]]; then
    "$haxeon_dir/scripts/build-native.sh"
fi

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$artifact" \
    --entry=ShowcaseDesktop \
    --root="$module_dir/examples/ui_showcase" \
    --root="$module_dir/bindings/haxe" \
    --root="$repo_dir/bindings/haxe" \
    --ffi-interface="$repo_dir/bindings/haxe/nativekit.hxi" \
    --ffi-projection="$repo_dir/bindings/haxe/nativekit.hxmap" \
    --ffi-interface="$module_dir/bindings/nativekit-ui.hxi" \
    --ffi-projection="$module_dir/bindings/nativekit-ui.hxmap" \
    --ffi-interface="$module_dir/bindings/nativekit-ui-showcase.hxi" \
    "$module_dir/examples/ui_showcase/ShowcaseDesktop.hx" \
    "$module_dir/examples/ui_showcase/Showcase.hx" \
    "$module_dir/examples/ui_showcase/ShowcaseCube.hx" \
    "$repo_dir/bindings/haxe/GraphicsImageRef.hx" \
    "$module_dir/bindings/haxe/"*.hx \
    "$repo_dir/bindings/haxe/NativeKitEvent.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventValue.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventContext.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventBytes.hx" \
    "$repo_dir/bindings/haxe/NativeKitWindowEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitInputEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitServiceEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitResourceEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitOptions.hx")

echo "showcase: built $artifact"
if [[ "$build_only" == true ]]; then
    exit 0
fi

font_path=${NKUI_TEST_FONT_PATH:-"$repo_dir/vendor/skribidi/example/data/IBMPlexSans-Regular.ttf"}
emoji_path=${NKUI_COLOR_FONT_PATH:-"$repo_dir/vendor/skribidi/example/data/NotoColorEmoji-Regular.ttf"}
runtime_library_path="$build_dir/modules/ui:$build_dir:$haxeon_dir/out:$haxeon_dir/vendor/hashlink"
hashlink_runtime="$haxeon_dir/vendor/hashlink/hl"
if [[ -x "$haxeon_dir/.tools/hashlink/hl" ]]; then
    hashlink_runtime="$haxeon_dir/.tools/hashlink/hl"
    runtime_library_path="$build_dir/modules/ui:$build_dir:$haxeon_dir/out:$haxeon_dir/.tools/hashlink"
fi
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
    runtime_library_path="$runtime_library_path:$LD_LIBRARY_PATH"
fi

(cd "$haxeon_dir/out" && \
    NKUI_TEST_FONT_PATH="$font_path" \
    NKUI_COLOR_FONT_PATH="$emoji_path" \
    LD_LIBRARY_PATH="$runtime_library_path" \
    "$hashlink_runtime" "$artifact" "${program_args[@]}")
