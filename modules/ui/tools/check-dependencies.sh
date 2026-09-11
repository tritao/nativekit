#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)

dependencies=(budouxc clay harfbuzz libunibreak nanovg sheenbidi skribidi sokol)
licenses=(
    budouxc/LICENSE
    clay/LICENSE.md
    harfbuzz/COPYING
    libunibreak/LICENCE
    nanovg/LICENSE.txt
    sheenbidi/LICENSE
    skribidi/LICENSE
    sokol/LICENSE
)

for dependency in "${dependencies[@]}"; do
    expected=$(git -C "$repo_dir" ls-tree HEAD "vendor/$dependency" | awk '{print $3}')
    # A newly added submodule is commonly staged before the feature commit
    # exists. Keep the development-tree audit useful while retaining the
    # committed gitlink as the CI source of truth once it is available.
    if [[ -z $expected ]]; then
        expected=$(git -C "$repo_dir" ls-files -s -- "vendor/$dependency" | awk 'NR == 1 {print $2}')
    fi
    if [[ -z $expected ]]; then
        echo "dependency is not pinned as a gitlink: $dependency" >&2
        exit 1
    fi
    actual=$(git -C "$repo_dir/vendor/$dependency" rev-parse HEAD)
    if [[ $actual != "$expected" ]]; then
        echo "$dependency revision mismatch: expected $expected, found $actual" >&2
        exit 1
    fi
done

for license in "${licenses[@]}"; do
    if [[ ! -s "$repo_dir/vendor/$license" ]]; then
        echo "missing or empty dependency license: vendor/$license" >&2
        exit 1
    fi
done

echo "PASS: UI dependency revisions and licenses are reproducible"
