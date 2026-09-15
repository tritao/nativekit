#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <cmake build directory> <install prefix>" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "package-native-release: Linux ELF packaging is currently supported" >&2
    exit 2
fi

build_dir=$1
prefix=$2

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "package-native-release: not a CMake build directory: $build_dir" >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1 || ! command -v objcopy >/dev/null 2>&1 || \
   ! command -v strip >/dev/null 2>&1; then
    echo "package-native-release: cmake, objcopy, and strip are required" >&2
    exit 2
fi

build_type=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$build_dir/CMakeCache.txt" | head -1)
if [[ -n "$build_type" && "$build_type" != "Release" && "$build_type" != "RelWithDebInfo" ]]; then
    echo "package-native-release: expected Release or RelWithDebInfo, got '$build_type'" >&2
    exit 1
fi

cmake --install "$build_dir" --prefix "$prefix" --config Release

lib_dir="$prefix/lib"
mapfile -t artifacts < <(find "$lib_dir" -maxdepth 1 -type f -name 'lib*.so.*' -print | sort)
if (( ${#artifacts[@]} == 0 )); then
    echo "package-native-release: no versioned shared libraries found in $lib_dir" >&2
    exit 1
fi

debug_dir="$lib_dir/.debug"
mkdir -p "$debug_dir"
for artifact in "${artifacts[@]}"; do
    name=$(basename "$artifact")
    debug_file="$debug_dir/$name.debug"
    objcopy --only-keep-debug "$artifact" "$debug_file"
    strip --strip-unneeded "$artifact"
    objcopy --add-gnu-debuglink="$debug_file" "$artifact"
    printf 'packaged %s (%s bytes) with symbols in %s\n' \
        "$artifact" "$(stat -c '%s' "$artifact")" "$debug_file"
done
