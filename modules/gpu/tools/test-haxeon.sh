#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
nativekit_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$nativekit_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$nativekit_dir/build-gpu"}

cmake -S "$nativekit_dir" -B "$build_dir" -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DNK_BUILD_GPU=ON -DNK_BUILD_TESTS=ON -DNK_BUILD_EXAMPLES=ON
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

hashlink_runtime="$haxeon_dir/.tools/hashlink/hl"
if [[ ! -x "$hashlink_runtime" ]]; then
	hashlink_runtime="$haxeon_dir/vendor/hashlink/hl"
fi
if [[ ! -x "$hashlink_runtime" ]]; then
	echo "test-haxeon: missing HashLink runtime in .tools/hashlink or vendor/hashlink" >&2
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
    --ffi-interface="$module_dir/bindings/nativekit-gpu.hxi" \
    --ffi-projection="$module_dir/bindings/nativekit-gpu.hxmap" \
    "$module_dir/tests/haxeon/Triangle.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Buffer.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/CommandBuffer.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Enums.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Image.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Pipeline.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Renderer.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Sampler.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Shader.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Surface.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/Uniforms.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/GpuResult.hx" \
    "$module_dir/bindings/haxe/nativekit/gpu/RenderTarget.hx" \
    "$nativekit_dir/bindings/haxe/GraphicsImageRef.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEvent.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventValue.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventContext.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventBytes.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWindowEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitInputEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitServiceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitResourceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRequests.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRequestOutcome.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWindow.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWebView.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRuntime.hx")

set +e
(cd "$haxeon_dir/out" && \
	LD_LIBRARY_PATH="$build_dir/modules/gpu:$build_dir:$haxeon_dir/out:$haxeon_dir/.tools/hashlink:$haxeon_dir/vendor/hashlink${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	xvfb-run -a "$hashlink_runtime" "$build_dir/haxeon-triangle.hl")
status=$?
set -e
if [[ $status -ne 42 ]]; then
	echo "test-haxeon: stress scene returned $status, expected 42" >&2
	exit 1
fi
echo "PASS: Haxeon rendered 400 textured quads through immediate and batched paths"
