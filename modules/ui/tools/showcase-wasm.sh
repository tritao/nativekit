#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
compiler_module=${NATIVEKIT_HAXEON_COMPILER_MODULE:-"$haxeon_dir/bootstrap/compiler.hl"}
build_dir=${NATIVEKIT_WASM_BUILD_DIR:-"$repo_dir/build-wasm"}
artifact="$build_dir/nativekit_ui_showcase_wasm32.wasm"
memory_contract=${NATIVEKIT_HAXEON_MEMORY_CONTRACT:-"$build_dir/nativekit_haxeon_memory_contract.json"}

if [[ $# -ne 0 ]]; then
    echo "usage: modules/ui/tools/showcase-wasm.sh" >&2
    exit 2
fi

if [[ ${NATIVEKIT_HAXEON_SELF_HOSTED:-0} == 1 ]]; then
	if [[ ! -x "$haxeon_dir/.tools/hashlink/hl" || ! -f "$compiler_module" ]]; then
		echo "showcase-wasm: self-hosted Haxeon compiler not found at $haxeon_dir" >&2
		exit 1
	fi
else
	if [[ ! -x "$haxeon_dir/.tools/haxe/haxe" ]]; then
		echo "showcase-wasm: Haxeon toolchain not found at $haxeon_dir" >&2
		echo "showcase-wasm: set HAXEON_DIR to the realtime-haxe checkout" >&2
		exit 1
	fi
fi
if [[ -n ${NATIVEKIT_HAXEON_DIAGNOSTICS_PORT:-} && ${NATIVEKIT_HAXEON_SELF_HOSTED:-0} != 1 ]]; then
	echo "showcase-wasm: diagnostics profiling requires NATIVEKIT_HAXEON_SELF_HOSTED=1" >&2
	exit 2
fi

if [[ ! -f "$memory_contract" ]]; then
    echo "showcase-wasm: memory contract not found at $memory_contract" >&2
    exit 1
fi

node - "$memory_contract" <<'NODE'
const fs = require('fs');
const path = process.argv[2];
const contract = JSON.parse(fs.readFileSync(path, 'utf8'));
const fields = ["version", "page_size", "host_base", "host_limit", "guest_base", "guest_limit", "memory_size"];
for (const field of fields) {
  if (!Number.isSafeInteger(contract[field]) || contract[field] < 0)
    throw new Error(`invalid integer field ${field}`);
}
if (contract.name !== "nativekit-haxeon-linear-memory" || contract.address_model !== "wasm32")
  throw new Error("invalid memory contract identity");
if (contract.page_size !== 65536 || contract.host_base !== 0 ||
    contract.host_limit !== contract.guest_base ||
    contract.guest_base >= contract.guest_limit ||
    contract.guest_limit !== contract.memory_size ||
    contract.memory_size % contract.page_size !== 0)
  throw new Error("invalid memory contract partition");
if (contract.version !== 1)
  throw new Error(`unsupported memory contract version ${contract.version}`);
console.log(`showcase-wasm: validated memory contract version ${contract.version}`);
NODE

mkdir -p "$build_dir"
if [[ ${NATIVEKIT_HAXEON_SKIP_HXI_CHECK:-0} == 1 ]]; then
	echo "showcase-wasm: skipped standalone HXI freshness audits"
else
	HAXEON_DIR="$haxeon_dir" "$repo_dir/tools/update-haxeon-wasm-hxi.sh" --check
	HAXEON_DIR="$haxeon_dir" "$module_dir/tools/update-haxeon-wasm-hxi.sh" --check
fi

compiler=("$haxeon_dir/.tools/haxe/haxe" -cp src --run compiler.tools.HaxeonCompiler)
if [[ ${NATIVEKIT_HAXEON_SELF_HOSTED:-0} == 1 ]]; then
	compiler=("$haxeon_dir/.tools/hashlink/hl")
	if [[ -n ${NATIVEKIT_HAXEON_DIAGNOSTICS_PORT:-} ]]; then
		compiler+=(--diagnostics "$NATIVEKIT_HAXEON_DIAGNOSTICS_PORT" --diagnostics-wait)
	fi
	compiler+=("$compiler_module")
fi

compiler_args=(
	--target=wasm32
	--output="$artifact"
	--entry=ShowcaseWeb
	--wasm-import-memory
	--wasm-memory-contract="$memory_contract"
	--export=ShowcaseWeb.main
	--export=ShowcaseWeb.configure
	--export=ShowcaseWeb.configureBenchmark
	--export=ShowcaseWeb.frame
	--export=ShowcaseWeb.status
	--export=ShowcaseWeb.caretOffset
	--export=ShowcaseWeb.caretAffinity
	--export=ShowcaseWeb.caretDirection
	--export=ShowcaseWeb.shutdown
	--root="$module_dir/examples/ui_showcase"
	--root="$module_dir/bindings/haxe"
	--root="$repo_dir/bindings/haxe"
	--ffi-interface="$repo_dir/bindings/haxe/nativekit-wasm.hxi"
	--ffi-projection="$repo_dir/bindings/haxe/nativekit.hxmap"
	--ffi-interface="$module_dir/bindings/nativekit-ui-wasm.hxi"
	--ffi-projection="$module_dir/bindings/nativekit-ui.hxmap"
	"$module_dir/examples/ui_showcase/Showcase.hx"
	"$module_dir/examples/ui_showcase/ShowcaseWeb.hx"
	"$module_dir/bindings/haxe/"*.hx
	"$repo_dir/bindings/haxe/NativeKitEvent.hx"
	"$repo_dir/bindings/haxe/NativeKitEventValue.hx"
	"$repo_dir/bindings/haxe/NativeKitEventContext.hx"
	"$repo_dir/bindings/haxe/NativeKitEventBytes.hx"
	"$repo_dir/bindings/haxe/NativeKitWindowEvents.hx"
	"$repo_dir/bindings/haxe/NativeKitInputEvents.hx"
	"$repo_dir/bindings/haxe/NativeKitServiceEvents.hx"
	"$repo_dir/bindings/haxe/NativeKitResourceEvents.hx"
	"$repo_dir/bindings/haxe/NativeKitOptions.hx"
)
if [[ ${NATIVEKIT_HAXEON_MEMORY_STATS:-0} == 1 ]]; then
	compiler_args+=(--wasm-memory-stats)
fi

if [[ -n ${NATIVEKIT_HAXEON_TIME_FILE:-} ]]; then
	(cd "$haxeon_dir" && /usr/bin/time -f 'wall=%e user=%U sys=%S maxrss_kb=%M' \
		-o "$NATIVEKIT_HAXEON_TIME_FILE" "${compiler[@]}" "${compiler_args[@]}")
else
	(cd "$haxeon_dir" && "${compiler[@]}" "${compiler_args[@]}")
fi

node - "$artifact" <<'NODE'
const fs = require('fs');
const path = process.argv[2];
const module = new WebAssembly.Module(fs.readFileSync(path));
const imports = WebAssembly.Module.imports(module);
const exports = WebAssembly.Module.exports(module).map(value => value.name);
const importsMemory = imports.some(value => value.kind === 'memory' && value.module === 'env' && value.name === 'memory');
if (!exports.includes('main') || (!exports.includes('memory') && !importsMemory))
    throw new Error('Showcase wasm is missing the main export or memory contract');
if (process.env.NATIVEKIT_HAXEON_MEMORY_STATS === '1') {
  for (const name of ['haxeon.memory.heap_base', 'haxeon.memory.heap_top',
      'haxeon.memory.metadata_base', 'haxeon.memory.metadata_top',
      'haxeon.memory.allocation_count', 'haxeon.memory.allocated_bytes'])
    if (!exports.includes(name)) throw new Error(`Showcase wasm is missing allocator diagnostic ${name}`);
}
console.log(`showcase-wasm: built ${path} (${imports.length} imports; exports ${exports.join(', ')})`);
NODE
