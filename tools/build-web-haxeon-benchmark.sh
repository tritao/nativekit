#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${NATIVEKIT_WEB_PROFILE_BUILD_DIR:-"$repo_dir/build-web-haxeon-profile"}

NKUI_HAXEON_MEMORY_STATS=ON \
NATIVEKIT_WEB_BUILD_DIR="$build_dir" \
"$repo_dir/tools/build-web.sh"
