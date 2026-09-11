#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
nativekit_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$nativekit_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$nativekit_dir/build-sokol-release"}

cmake -S "$nativekit_dir" -B "$build_dir" -GNinja -DCMAKE_BUILD_TYPE=Release \
    -DNK_BUILD_SOKOL=ON -DNK_BUILD_TESTS=ON -DNK_BUILD_EXAMPLES=OFF
cmake --build "$build_dir" --target nativekit_sokol_benchmark

"$haxeon_dir/scripts/haxeon-ffi-audit" \
    --target=x86_64-linux-gnu \
    --target=x86_64-w64-windows-gnu \
    --target=x86_64-apple-darwin \
    --target=arm64-apple-darwin \
    --profile=portable-abi64 \
    --library=nativekit_sokol_benchmark \
    --interface=NativeKitSokolBenchmark \
    --include="$module_dir/bench" \
    --source-label=modules/sokol/bench/benchmark_import.h \
    --output="$build_dir/nativekit-sokol-benchmark.hxi" \
    "$module_dir/bench/benchmark_import.h"

"$haxeon_dir/scripts/build-runtime.sh"
(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/nativekit-sokol-benchmark.hl" \
    --entry=Benchmark \
    --root="$module_dir/bench" \
    --ffi-interface="$build_dir/nativekit-sokol-benchmark.hxi" \
    "$module_dir/bench/Benchmark.hx")

set +e
(cd "$haxeon_dir/out" && \
    LD_LIBRARY_PATH="$build_dir/modules/sokol${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$haxeon_dir/vendor/hashlink/hl" "$build_dir/nativekit-sokol-benchmark.hl")
status=$?
set -e
if [[ $status -ne 42 ]]; then
    echo "benchmark: returned $status, expected 42" >&2
    exit 1
fi
