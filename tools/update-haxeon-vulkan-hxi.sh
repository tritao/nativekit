#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output="$repo_dir/bindings/haxe/nativekit-vulkan.hxi"
destination=$output

if [[ ${1:-} == "--check" ]]; then
    destination=$(mktemp)
    trap 'rm -f -- "$destination"' EXIT
elif [[ $# -ne 0 ]]; then
    echo "usage: tools/update-haxeon-vulkan-hxi.sh [--check]" >&2
    exit 2
fi

cd "$repo_dir"
"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit \
    --interface=NativeKitVulkan \
    --depends=NativeKit \
    --dependency-hxi=bindings/haxe/nativekit.hxi \
    --include=include \
    --source-label=include/nativekit_vulkan.h \
    --exclude-header=include/nativekit.h \
    --output="$destination" \
    include/nativekit_vulkan.h

if [[ ${1:-} == "--check" ]] && ! cmp -s "$output" "$destination"; then
    echo "Haxeon Vulkan binding is stale; run tools/update-haxeon-vulkan-hxi.sh" >&2
    diff -u "$output" "$destination" || true
    exit 1
fi
