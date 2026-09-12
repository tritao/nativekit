#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output=${1:-"$module_dir/bindings/nativekit-sokol.hxi"}

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit_sokol \
    --interface=NativeKitSokol \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit.hxi" \
    --include="$module_dir/include" \
    --include="$module_dir/bindings" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --exclude-header="$repo_dir/include/nativekit_graphics.h" \
    --source-label=modules/sokol/bindings/nativekit_sokol_import.h \
    --output="$output" \
    "$module_dir/bindings/nativekit_sokol_import.h"

echo "check-hxi: wrote $output"
