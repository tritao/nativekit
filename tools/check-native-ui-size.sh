#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <nativekit_ui shared library> [max loadable bytes]" >&2
    exit 2
fi

artifact=$1
budget=${2:-1700000}

if [[ ! -f "$artifact" ]]; then
    echo "check-native-ui-size: artifact not found: $artifact" >&2
    exit 1
fi
if [[ ! "$budget" =~ ^[0-9]+$ ]]; then
    echo "check-native-ui-size: budget must be a non-negative integer" >&2
    exit 2
fi
if ! command -v size >/dev/null 2>&1; then
    echo "check-native-ui-size: 'size' is required" >&2
    exit 2
fi

read -r text data bss total _ < <(size "$artifact" | awk 'NR == 2 { print $1, $2, $3, $4, $5 }')
if [[ ! "$total" =~ ^[0-9]+$ ]]; then
    echo "check-native-ui-size: could not parse size output for: $artifact" >&2
    size "$artifact" >&2
    exit 2
fi

printf 'nativekit_ui: text=%s data=%s bss=%s loadable=%s bytes (budget=%s)\n' \
    "$text" "$data" "$bss" "$total" "$budget"

if (( total > budget )); then
    echo "check-native-ui-size: budget exceeded by $((total - budget)) bytes" >&2
    exit 1
fi
