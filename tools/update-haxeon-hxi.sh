#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
output="$repo_dir/bindings/haxe/nativekit.hxi"
destination=$output

if [[ ${1:-} == "--check" ]]; then
    destination=$(mktemp)
    trap 'rm -f -- "$destination"' EXIT
elif [[ $# -ne 0 ]]; then
    echo "usage: tools/update-haxeon-hxi.sh [--check]" >&2
    exit 2
fi

HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/audit-haxeon-abi.sh" --output="$destination"

if [[ ${1:-} == "--check" ]] && ! cmp -s "$output" "$destination"; then
    echo "Haxeon binding is stale; run tools/update-haxeon-hxi.sh" >&2
    diff -u "$output" "$destination" || true
    exit 1
fi
