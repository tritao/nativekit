#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
"$repo_dir/tools/setup-emscripten.sh"

echo
echo "Web toolchain is ready. Configure NativeKit with:"
echo "  source \"$repo_dir/.tools/emsdk/emsdk_env.sh\""
echo "  emcmake cmake -S \"$repo_dir\" -B \"$repo_dir/build-web\" -GNinja"
