#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$module_dir/tools/generate-shaders.py" "$@"
