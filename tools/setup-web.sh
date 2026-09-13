#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
"$repo_dir/tools/setup-emscripten.sh"

echo
echo "Web toolchain is ready. Use tools/build-web.sh to build the browser examples."
