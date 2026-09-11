#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output="$repo_dir/bindings/haxe/nativekit-wasm.hxi"
destination=$output

if [[ ${1:-} == "--check" ]]; then
    destination=$(mktemp)
    trap 'rm -f -- "$destination"' EXIT
elif [[ $# -ne 0 ]]; then
    echo "usage: tools/update-haxeon-wasm-hxi.sh [--check]" >&2
    exit 2
fi

HAXEON_DIR="$haxeon_dir" "$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=wasm32-unknown-wasi \
    --target=wasm32-unknown-emscripten \
    --profile=portable-abi32 \
    --library=nativekit \
    --interface=NativeKit \
    --include="$repo_dir/include" \
    --source-label=bindings/haxe/nativekit_import.h \
    --output="$destination" \
    "$repo_dir/bindings/haxe/nativekit_import.h"

if [[ ${1:-} == "--check" ]] && ! cmp -s "$output" "$destination"; then
    echo "Haxeon wasm binding is stale; run tools/update-haxeon-wasm-hxi.sh" >&2
    diff -u "$output" "$destination" || true
    exit 1
fi
