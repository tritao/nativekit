#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output=${1:-"$module_dir/bindings/nativekit-sokol.hxi"}

"$haxeon_dir/scripts/haxeon-ffi-import" \
    --target=x86_64-linux-gnu \
    --library=nativekit_sokol \
    --interface=NativeKitSokol \
    --include="$module_dir/include" \
    --include="$module_dir/bindings" \
    --source-label=modules/sokol/bindings/nativekit_sokol_import.h \
    --output="$output" \
    "$module_dir/bindings/nativekit_sokol_import.h"

echo "check-hxi: wrote $output"
