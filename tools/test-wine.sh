#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$repo_dir/build-mingw"}

if ! command -v x86_64-w64-mingw32-gcc >/dev/null &&
   ! command -v x86_64-w64-mingw32-gcc-posix >/dev/null; then
    echo "test-wine: missing MinGW C compiler" >&2
    exit 2
fi
if ! command -v x86_64-w64-mingw32-g++ >/dev/null &&
   ! command -v x86_64-w64-mingw32-g++-posix >/dev/null; then
    echo "test-wine: missing MinGW C++ compiler" >&2
    exit 2
fi
for command in wine wineserver xvfb-run; do
    command -v "$command" >/dev/null || {
        echo "test-wine: missing required command: $command" >&2
        exit 2
    }
done

cmake -S "$repo_dir" -B "$build_dir" -GNinja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_dir/cmake/toolchains/mingw-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Debug -DNK_BUILD_SHARED=OFF \
    -DNK_BUILD_EXAMPLES=OFF -DNK_BUILD_TESTS=ON
cmake --build "$build_dir"

wine_prefix=$(mktemp -d)
cleanup() {
    WINEPREFIX="$wine_prefix" wineserver -k >/dev/null 2>&1 || true
    rm -rf -- "$wine_prefix"
}
trap cleanup EXIT

export WINEPREFIX="$wine_prefix"
export WINEDEBUG=-all
xvfb-run -a bash -c '
    wine "$1"
    wine "$2"
    wine "$3"
' bash \
    "$build_dir/tests/nativekit_c_abi.exe" \
    "$build_dir/tests/nativekit_core_tests.exe" \
    "$build_dir/tests/nativekit_win_integration.exe"
