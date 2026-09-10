#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}

cd "$repo_dir"

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit_ui \
    --interface=NativeKitUI \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit.hxi" \
    --include="$module_dir/include" \
    --include="$module_dir/bindings" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --source-label=modules/ui/bindings/nativekit_ui_import.h \
    "$@" \
    "$module_dir/bindings/nativekit_ui_import.h"
