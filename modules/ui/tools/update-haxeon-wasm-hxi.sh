#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output="$module_dir/bindings/nativekit-ui-wasm.hxi"
destination=$output

if [[ ${1:-} == "--check" ]]; then
    destination=$(mktemp)
    trap 'rm -f -- "$destination"' EXIT
elif [[ $# -ne 0 ]]; then
    echo "usage: modules/ui/tools/update-haxeon-wasm-hxi.sh [--check]" >&2
    exit 2
fi

HAXEON_DIR="$haxeon_dir" "$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=wasm32-unknown-wasi \
    --target=wasm32-unknown-emscripten \
    --profile=portable-abi32 \
    --library=nativekit_ui \
    --interface=NativeKitUI \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit-wasm.hxi" \
    --include="$module_dir/include" \
    --include="$module_dir/bindings" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --source-label=modules/ui/bindings/nativekit_ui_import.h \
    --output="$destination" \
    "$module_dir/bindings/nativekit_ui_import.h"

if [[ ${1:-} == "--check" ]] && ! cmp -s "$output" "$destination"; then
    echo "Haxeon wasm UI binding is stale; run modules/ui/tools/update-haxeon-wasm-hxi.sh" >&2
    diff -u "$output" "$destination" || true
    exit 1
fi
