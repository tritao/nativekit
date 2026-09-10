#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$repo_dir/build-ui"}

cmake --build "$build_dir" --target nativekit_ui
"$module_dir/tools/check-hxi.sh"
"$haxeon_dir/scripts/build-runtime.sh"

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/haxeon-ui-transaction.hl" \
    --entry=Transaction \
    --root="$module_dir/tests/haxeon" \
    --root="$module_dir/bindings/haxe" \
    --ffi-interface="$module_dir/bindings/nativekit-ui-linux-x86_64.hxi" \
    "$module_dir/tests/haxeon/Transaction.hx" \
    "$module_dir/bindings/haxe/CanvasCommandBuffer.hx")

(cd "$haxeon_dir/out" && \
    LD_LIBRARY_PATH="$build_dir/modules/ui:$build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$haxeon_dir/vendor/hashlink/hl" "$build_dir/haxeon-ui-transaction.hl")

echo "PASS: Haxeon submitted a validated NativeKit UI transaction in one FFI call"
