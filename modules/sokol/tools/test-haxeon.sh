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
if [[ -x "$haxeon_dir/scripts/build-runtime.sh" ]]; then
	"$haxeon_dir/scripts/build-runtime.sh"
elif [[ -x "$haxeon_dir/scripts/build-native.sh" ]]; then
	(cd "$haxeon_dir" && ./scripts/build-native.sh)
else
	echo "test-haxeon: Haxeon runtime build script not found" >&2
	exit 2
fi

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/haxeon-triangle.hl" \
    --entry=Triangle \
    --root="$module_dir/tests/haxeon" \
    --root="$module_dir/bindings/haxe" \
    --root="$nativekit_dir/bindings/haxe" \
    --ffi-interface="$nativekit_dir/bindings/haxe/nativekit.hxi" \
    --ffi-projection="$nativekit_dir/bindings/haxe/nativekit.hxmap" \
    --ffi-interface="$module_dir/bindings/nativekit-sokol.hxi" \
    --ffi-projection="$module_dir/bindings/nativekit-sokol.hxmap" \
    "$module_dir/tests/haxeon/Triangle.hx" \
    "$module_dir/bindings/haxe/SokolBuffer.hx" \
    "$module_dir/bindings/haxe/SokolCommandBuffer.hx" \
    "$module_dir/bindings/haxe/SokolEnums.hx" \
    "$module_dir/bindings/haxe/SokolImage.hx" \
    "$module_dir/bindings/haxe/SokolPipeline.hx" \
    "$module_dir/bindings/haxe/SokolRenderer.hx" \
    "$module_dir/bindings/haxe/SokolSampler.hx" \
    "$module_dir/bindings/haxe/SokolShader.hx" \
    "$module_dir/bindings/haxe/SokolSurface.hx" \
    "$module_dir/bindings/haxe/SokolUniforms.hx" \
    "$module_dir/bindings/haxe/SokolResult.hx" \
    "$module_dir/bindings/haxe/SokolRenderTarget.hx" \
    "$nativekit_dir/bindings/haxe/GraphicsImageRef.hx" \
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
