#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

if [[ ! -x "$haxeon_dir/.tools/haxe/haxe" ]]; then
    echo "test-haxe-bindings: missing Haxeon compiler: $haxeon_dir/.tools/haxe/haxe" >&2
    exit 2
fi

HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-hxi.sh" --check
HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-vulkan-hxi.sh" --check
HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-net-hxi.sh" --check
HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-wasm-hxi.sh" --check
HAXEON_DIR="$haxeon_dir" "$repo_dir/modules/ui/tools/update-haxeon-wasm-hxi.sh" --check

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit_gpu \
    --interface=NativeKitGpu \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit.hxi" \
    --include="$repo_dir/modules/gpu/include" \
    --include="$repo_dir/modules/gpu/bindings" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --exclude-header="$repo_dir/include/nativekit_graphics.h" \
    --source-label=modules/gpu/bindings/nativekit_gpu_import.h \
    --output="$test_root/nativekit-gpu-abi64.hxi" \
    "$repo_dir/modules/gpu/bindings/nativekit_gpu_import.h"
cmp "$repo_dir/modules/gpu/bindings/nativekit-gpu.hxi" "$test_root/nativekit-gpu-abi64.hxi"

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=wasm32-unknown-wasi \
    --target=wasm32-unknown-emscripten \
    --profile=portable-abi32 \
    --library=nativekit_gpu \
    --interface=NativeKitGpu \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit-wasm.hxi" \
    --include="$repo_dir/modules/gpu/include" \
    --include="$repo_dir/modules/gpu/bindings" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --exclude-header="$repo_dir/include/nativekit_graphics.h" \
    --source-label=modules/gpu/bindings/nativekit_gpu_import.h \
    --output="$test_root/nativekit-gpu-abi32.hxi" \
    "$repo_dir/modules/gpu/bindings/nativekit_gpu_import.h"

HAXEON_DIR="$haxeon_dir" "$repo_dir/modules/ui/tools/check-hxi.sh" \
    "$test_root/nativekit-ui-abi64.hxi"
cmp "$repo_dir/modules/ui/bindings/nativekit-ui.hxi" "$test_root/nativekit-ui-abi64.hxi"

build_dir="$test_root/native-build"
cmake -S "$repo_dir" -B "$build_dir" -GNinja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNK_BUILD_SHARED=ON \
    -DNK_BUILD_GPU=ON \
    -DNK_BUILD_UI=ON \
    -DNK_BUILD_TESTS=ON \
    -DNK_BUILD_EXAMPLES=ON

HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/test-haxeon.sh"
HAXEON_DIR="$haxeon_dir" NATIVEKIT_BUILD_DIR="$build_dir" \
    "$repo_dir/modules/gpu/tools/test-haxeon.sh"
HAXEON_DIR="$haxeon_dir" NATIVEKIT_BUILD="$build_dir" \
    "$repo_dir/modules/scene_render/tools/test-haxeon.sh"
HAXEON_DIR="$haxeon_dir" NATIVEKIT_BUILD_DIR="$build_dir" \
    "$repo_dir/modules/ui/tools/test-haxeon.sh"
HAXEON_DIR="$haxeon_dir" NATIVEKIT_BUILD_DIR="$build_dir" \
    "$repo_dir/modules/ui/tools/test-haxeon-framework.sh"
HAXEON_DIR="$haxeon_dir" NATIVEKIT_BUILD_DIR="$build_dir" \
    "$repo_dir/modules/ui/tools/showcase.sh" --build-only

echo "PASS: ABI32/ABI64 HXI drift checks and NativeKit core, GPU, and UI Haxeon smoke tests"
