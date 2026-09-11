#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sokol_tools_dir="$repo_dir/.tools/sokol-tools-bin"
sokol_tools_ref=${SOKOL_TOOLS_REF:-11d0cf678105d614d675e6d9bd2aaf3eeff12f8c}

"$repo_dir/tools/setup-emscripten.sh"

mkdir -p "$repo_dir/.tools"
if [[ ! -d "$sokol_tools_dir/.git" ]]; then
    if [[ -e "$sokol_tools_dir" ]]; then
        echo "setup-web: refusing to replace $sokol_tools_dir" >&2
        exit 1
    fi
    git clone --filter=blob:none https://github.com/floooh/sokol-tools-bin.git \
        "$sokol_tools_dir"
fi

git -C "$sokol_tools_dir" fetch --depth 1 origin "$sokol_tools_ref"
git -C "$sokol_tools_dir" checkout --detach "$sokol_tools_ref"

case "$(uname -s):$(uname -m)" in
    Linux:x86_64) shdc="$sokol_tools_dir/bin/linux/sokol-shdc" ;;
    Linux:aarch64|Linux:arm64) shdc="$sokol_tools_dir/bin/linux_arm64/sokol-shdc" ;;
    Darwin:x86_64) shdc="$sokol_tools_dir/bin/osx/sokol-shdc" ;;
    Darwin:arm64) shdc="$sokol_tools_dir/bin/osx_arm64/sokol-shdc" ;;
    MINGW*:x86_64|MSYS*:x86_64|CYGWIN*:x86_64) shdc="$sokol_tools_dir/bin/win32/sokol-shdc.exe" ;;
    *)
        echo "setup-web: no bundled sokol-shdc binary for $(uname -s)/$(uname -m)" >&2
        exit 1
        ;;
esac

if [[ ! -x "$shdc" ]]; then
    echo "setup-web: expected shader compiler is missing: $shdc" >&2
    exit 1
fi

echo
echo "Web toolchain is ready. Use tools/build-web.sh, or set:"
echo "  NK_SOKOL_SHDC=$shdc"
