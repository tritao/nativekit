#!/usr/bin/env bash
set -euo pipefail
nativekit_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$nativekit_dir/.tools/emsdk/emsdk_env.sh" >/dev/null
build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT
emcmake cmake -S "$nativekit_dir/tests/wasm_allocator_workers" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel 4
node "$build_dir/allocator-workers.js"
