#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)

dependencies=(budouxc harfbuzz libunibreak nanovg sheenbidi skribidi sokol)
licenses=(
    budouxc/LICENSE
    harfbuzz/COPYING
    libunibreak/LICENCE
    nanovg/LICENSE.txt
    sheenbidi/LICENSE
    skribidi/LICENSE
    sokol/LICENSE
)

for dependency in "${dependencies[@]}"; do
    expected=$(git -C "$repo_dir" ls-tree HEAD "vendor/$dependency" | awk '{print $3}')
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
