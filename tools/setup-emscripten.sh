#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
emsdk_dir="$repo_dir/.tools/emsdk"
version=${EMSDK_VERSION:-6.0.9}

mkdir -p "$repo_dir/.tools"
if [[ ! -d "$emsdk_dir/.git" ]]; then
    if [[ -e "$emsdk_dir" ]]; then
        echo "setup-emscripten: refusing to replace $emsdk_dir" >&2
        exit 1
    fi
    git clone --depth 1 --branch "$version" \
        https://github.com/emscripten-core/emsdk.git "$emsdk_dir"
fi

"$emsdk_dir/emsdk" install "$version"
"$emsdk_dir/emsdk" activate "$version"

echo
echo "Emscripten $version is ready. Activate it in each shell with:"
echo "  source \"$emsdk_dir/emsdk_env.sh\""
