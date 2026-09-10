#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
nativekit_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$nativekit_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$nativekit_dir/build-sokol"}

cmake -S "$nativekit_dir" -B "$build_dir" -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DNK_BUILD_SOKOL=ON -DNK_BUILD_TESTS=ON -DNK_BUILD_EXAMPLES=ON
cmake --build "$build_dir"
"$module_dir/tools/check-hxi.sh"
"$haxeon_dir/scripts/build-runtime.sh"

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/haxeon-triangle.hl" \
    --entry=Triangle \
    --root="$module_dir/tests/haxeon" \
    --root="$module_dir/bindings/haxe" \
    --root="$nativekit_dir/bindings/haxe" \
    --ffi-interface="$nativekit_dir/bindings/haxe/nativekit.hxi" \
    --ffi-interface="$module_dir/bindings/nativekit-sokol.hxi" \
    "$module_dir/tests/haxeon/Triangle.hx" \
    "$module_dir/bindings/haxe/SokolCommandBuffer.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEvent.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventValue.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventContext.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventBytes.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWindowEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitInputEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitServiceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitResourceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitOptions.hx")

set +e
(cd "$haxeon_dir/out" && \
    LD_LIBRARY_PATH="$build_dir/modules/sokol:$build_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    xvfb-run -a "$haxeon_dir/vendor/hashlink/hl" "$build_dir/haxeon-triangle.hl")
status=$?
set -e
if [[ $status -ne 42 ]]; then
	echo "test-haxeon: stress scene returned $status, expected 42" >&2
	exit 1
fi
echo "PASS: Haxeon rendered 400 textured quads through immediate and batched paths"
