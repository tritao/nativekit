#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$repo_dir/build-ui"}

cmake --build "$build_dir" --target nativekit_ui
"$module_dir/tools/check-hxi.sh"
if [[ -x "$haxeon_dir/scripts/build-runtime.sh" ]]; then
    "$haxeon_dir/scripts/build-runtime.sh"
elif [[ -x "$haxeon_dir/scripts/build-native.sh" ]]; then
    (cd "$haxeon_dir" && ./scripts/build-native.sh)
else
    echo "test-haxeon: Haxeon runtime build script not found" >&2
    exit 2
fi

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/haxeon-ui-transaction.hl" \
    --entry=Transaction \
    --root="$module_dir/tests/haxeon" \
    --root="$module_dir/bindings/haxe" \
    --ffi-interface="$repo_dir/bindings/haxe/nativekit.hxi" \
    --ffi-projection="$repo_dir/bindings/haxe/nativekit.hxmap" \
    --ffi-interface="$module_dir/bindings/nativekit-ui.hxi" \
    --ffi-projection="$module_dir/bindings/nativekit-ui.hxmap" \
    "$module_dir/tests/haxeon/Transaction.hx" \
    "$module_dir/bindings/haxe/"*.hx)

(cd "$haxeon_dir/out" && \
    NKUI_TEST_FONT_PATH="$repo_dir/vendor/skribidi/example/data/IBMPlexSans-Regular.ttf" \
    LD_LIBRARY_PATH="$build_dir/modules/ui:$build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    xvfb-run -a "$haxeon_dir/vendor/hashlink/hl" "$build_dir/haxeon-ui-transaction.hl")

echo "PASS: Haxeon rendered a validated Canvas transaction through NativeKit UI and Sokol"
