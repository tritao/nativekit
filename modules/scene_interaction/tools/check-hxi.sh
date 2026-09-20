#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output=${1:-"$module_dir/bindings/nativekit-scene-interaction.hxi"}

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit_scene_interaction \
    --interface=NativeKitSceneInteraction \
    --depends=NativeKitSceneRender \
    --depends=NativeKitScene \
    --depends=NativeKitGpu \
    --dependency-hxi="$repo_dir/modules/scene/bindings/nativekit-scene.hxi" \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit.hxi" \
    --dependency-hxi="$repo_dir/modules/gpu/bindings/nativekit-gpu.hxi" \
    --dependency-hxi="$repo_dir/modules/scene_render/bindings/nativekit-scene-render.hxi" \
    --include="$module_dir/include" \
    --include="$repo_dir/modules/scene_render/include" \
    --include="$repo_dir/modules/scene/include" \
    --include="$repo_dir/modules/gpu/include" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/modules/scene_render/include/nativekit_scene_render.h" \
    --exclude-header="$repo_dir/modules/scene/include/nativekit_scene.h" \
    --exclude-header="$repo_dir/modules/gpu/include/nativekit_gpu.h" \
    --exclude-header="$repo_dir/include/nativekit_graphics.h" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --source-label=modules/scene_interaction/bindings/nativekit_scene_interaction_import.h \
    --output="$output" \
    "$module_dir/bindings/nativekit_scene_interaction_import.h"

echo "check-hxi: wrote $output"
