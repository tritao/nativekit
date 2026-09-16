#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
build_dir=${NATIVEKIT_BUILD_DIR:-"$repo_dir/build-ui"}
iterations=${TEXT_EDITOR_BENCHMARK_ITERATIONS:-5}

if [[ $# -gt 0 ]]; then
	iterations=$1
fi

if [[ ! "$iterations" =~ ^[1-9][0-9]*$ ]]; then
	echo "usage: benchmark-text-editor.sh [iterations]" >&2
	exit 2
fi

cmake --build "$build_dir" --target nativekit_ui_text_editor_benchmark
"$build_dir/modules/ui/nativekit_ui_text_editor_benchmark" --iterations "$iterations"
