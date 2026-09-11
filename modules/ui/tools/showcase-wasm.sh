#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_WASM_BUILD_DIR:-"$repo_dir/build-wasm"}
artifact="$build_dir/nativekit_ui_showcase_wasm32.wasm"

if [[ $# -ne 0 ]]; then
    echo "usage: modules/ui/tools/showcase-wasm.sh" >&2
    exit 2
fi

if [[ ! -x "$haxeon_dir/.tools/haxe/haxe" ]]; then
    echo "showcase-wasm: Haxeon toolchain not found at $haxeon_dir" >&2
    echo "showcase-wasm: set HAXEON_DIR to the realtime-haxe checkout" >&2
    exit 1
fi

mkdir -p "$build_dir"
HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-wasm-hxi.sh" --check
HAXEON_DIR="$haxeon_dir" "$module_dir/tools/update-haxeon-wasm-hxi.sh" --check

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --target=wasm32 \
    --output="$artifact" \
    --entry=Showcase \
    --root="$module_dir/examples/ui_showcase" \
    --root="$module_dir/bindings/haxe" \
    --root="$repo_dir/bindings/haxe" \
    --ffi-interface="$repo_dir/bindings/haxe/nativekit-wasm.hxi" \
    --ffi-interface="$module_dir/bindings/nativekit-ui-wasm.hxi" \
    "$module_dir/examples/ui_showcase/Showcase.hx" \
    "$module_dir/bindings/haxe/"*.hx \
    "$repo_dir/bindings/haxe/NativeKitEvent.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventValue.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventContext.hx" \
    "$repo_dir/bindings/haxe/NativeKitEventBytes.hx" \
    "$repo_dir/bindings/haxe/NativeKitWindowEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitInputEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitServiceEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitResourceEvents.hx" \
    "$repo_dir/bindings/haxe/NativeKitOptions.hx")

node - "$artifact" <<'NODE'
const fs = require('fs');
const path = process.argv[2];
const module = new WebAssembly.Module(fs.readFileSync(path));
const imports = WebAssembly.Module.imports(module);
const exports = WebAssembly.Module.exports(module).map(value => value.name);
if (!exports.includes('main') || !exports.includes('memory'))
    throw new Error('Showcase wasm is missing the main or memory export');
console.log(`showcase-wasm: built ${path} (${imports.length} imports; exports ${exports.join(', ')})`);
NODE
