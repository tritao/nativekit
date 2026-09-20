#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$repo_dir/build-ui"}
artifact="$build_dir/haxeon-ui-virtual-list-benchmark.hl"

cmake --build "$build_dir" --target nativekit_ui
"$module_dir/tools/check-hxi.sh"
if [[ -x "$haxeon_dir/scripts/build-runtime.sh" ]]; then
	(cd "$haxeon_dir" && scripts/build-runtime.sh)
elif [[ -x "$haxeon_dir/scripts/build-native.sh" ]]; then
	(cd "$haxeon_dir" && scripts/build-native.sh)
else
	echo "virtual-list benchmark: Haxeon runtime build script not found" >&2
	exit 2
fi

hashlink_runtime="$haxeon_dir/.tools/hashlink/hl"
if [[ ! -x "$hashlink_runtime" ]]; then
	hashlink_runtime="$haxeon_dir/vendor/hashlink/hl"
fi
if [[ ! -x "$hashlink_runtime" ]]; then
	echo "virtual-list benchmark: missing HashLink runtime" >&2
	exit 2
fi

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
	--output="$artifact" \
	--entry=VirtualListBenchmark \
	--root="$module_dir/bench" \
	--root="$module_dir/haxe" \
	--root="$module_dir/bindings/haxe" \
	--root="$repo_dir/bindings/haxe" \
	--ffi-interface="$repo_dir/bindings/haxe/nativekit.hxi" \
	--ffi-projection="$repo_dir/bindings/haxe/nativekit.hxmap" \
	--ffi-interface="$module_dir/bindings/nativekit-ui.hxi" \
	--ffi-projection="$module_dir/bindings/nativekit-ui.hxmap" \
	"$module_dir/bench/VirtualListBenchmark.hx" \
	"$module_dir/haxe/nativekit/ui/core/"*.hx \
	"$module_dir/haxe/nativekit/ui/style/"*.hx \
	"$module_dir/haxe/nativekit/ui/widgets/"*.hx \
	"$module_dir/bindings/haxe/"*.hx)

font_path=${NKUI_TEST_FONT_PATH:-"$repo_dir/vendor/skribidi/example/data/IBMPlexSans-Regular.ttf"}
runtime_library_path="$build_dir/modules/ui:$build_dir:$haxeon_dir/out:$haxeon_dir/.tools/hashlink:$haxeon_dir/vendor/hashlink"
if [[ -n "${LD_LIBRARY_PATH:-}" ]]; then
	runtime_library_path="$runtime_library_path:$LD_LIBRARY_PATH"
fi

(cd "$haxeon_dir/out" && \
	NKUI_TEST_FONT_PATH="$font_path" \
	LD_LIBRARY_PATH="$runtime_library_path" \
	"$hashlink_runtime" "$artifact")
