#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}

for path in "$haxeon_dir/.tools/haxe/haxe" "$haxeon_dir/vendor/hashlink/hl"; do
    if [[ ! -x "$path" ]]; then
        echo "test-haxeon: missing Haxeon toolchain executable: $path" >&2
        exit 2
    fi
done

test_root=$(mktemp -d)
cleanup() {
    rm -rf -- "$test_root"
}
trap cleanup EXIT

HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-hxi.sh" --check

cmake -S "$repo_dir" -B "$test_root/build" -GNinja \
    -DCMAKE_BUILD_TYPE=Debug -DNK_BUILD_SHARED=ON \
    -DNK_BUILD_EXAMPLES=OFF -DNK_BUILD_TESTS=OFF
cmake --build "$test_root/build"
"$haxeon_dir/scripts/build-runtime.sh"

(
    cd "$haxeon_dir"
    .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
        --output="$test_root/nativekit-smoke.hl" \
        --entry=Smoke \
        --root="$repo_dir/tests/haxeon" \
        --root="$repo_dir/bindings/haxe" \
        --ffi-interface="$repo_dir/bindings/haxe/nativekit-linux-x86_64.hxi" \
        "$repo_dir/tests/haxeon/Smoke.hx" \
        "$repo_dir/bindings/haxe/NativeKitEvent.hx" \
        "$repo_dir/bindings/haxe/NativeKitEventValue.hx" \
        "$repo_dir/bindings/haxe/NativeKitEventContext.hx" \
		"$repo_dir/bindings/haxe/NativeKitEventBytes.hx" \
		"$repo_dir/bindings/haxe/NativeKitEventDecoderTests.hx" \
        "$repo_dir/bindings/haxe/NativeKitWindowEvents.hx" \
        "$repo_dir/bindings/haxe/NativeKitInputEvents.hx" \
        "$repo_dir/bindings/haxe/NativeKitServiceEvents.hx" \
        "$repo_dir/bindings/haxe/NativeKitResourceEvents.hx" \
        "$repo_dir/bindings/haxe/NativeKitRequests.hx"
)

set +e
runtime_library_path="$test_root/build:$haxeon_dir/out:$haxeon_dir/vendor/hashlink${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if command -v xvfb-run >/dev/null; then
    xvfb-run -a env LD_LIBRARY_PATH="$runtime_library_path" \
        "$haxeon_dir/vendor/hashlink/hl" "$test_root/nativekit-smoke.hl"
else
    LD_LIBRARY_PATH="$runtime_library_path" \
        "$haxeon_dir/vendor/hashlink/hl" "$test_root/nativekit-smoke.hl"
fi
status=$?
set -e
if [[ $status -ne 42 ]]; then
    echo "test-haxeon: smoke test returned $status, expected 42" >&2
    exit 1
fi

echo "PASS: generated Haxeon bindings exercised NativeKit lifecycle, copied event payloads, UTF-8, handles, and output buffers"
