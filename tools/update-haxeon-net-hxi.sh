#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/haxeon"}
output="$repo_dir/bindings/haxe/nativekit-net.hxi"

if [[ ${1:-} == "--check" ]]; then
    destination=$(mktemp)
    trap 'rm -f -- "$destination"' EXIT
elif [[ $# -ne 0 ]]; then
    echo "usage: tools/update-haxeon-net-hxi.sh [--check]" >&2
    exit 2
else
    destination="$output"
fi

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit \
    --interface=NativeKitNet \
    --depends=NativeKit \
    --dependency-hxi="$repo_dir/bindings/haxe/nativekit.hxi" \
    --include="$repo_dir/include" \
    --exclude-header="$repo_dir/include/nativekit.h" \
    --source-label=bindings/haxe/nativekit_net_import.h \
    --output="$destination" \
    "$repo_dir/bindings/haxe/nativekit_net_import.h"

if [[ ${1:-} == "--check" ]] && ! cmp -s "$output" "$destination"; then
    echo "Haxeon net binding is stale; run tools/update-haxeon-net-hxi.sh" >&2
    diff -u "$output" "$destination" || true
    exit 1
fi

echo "update-haxeon-net-hxi: wrote $output"
