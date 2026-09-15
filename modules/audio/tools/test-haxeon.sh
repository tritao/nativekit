#!/usr/bin/env bash
set -euo pipefail

module_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
nativekit_dir=$(cd "$module_dir/../.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$nativekit_dir")/realtime-haxe"}
build_dir=${NATIVEKIT_BUILD_DIR:-"$nativekit_dir/build-audio"}

cmake -S "$nativekit_dir" -B "$build_dir" -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DNK_BUILD_AUDIO=ON -DNK_BUILD_TESTS=ON -DNK_BUILD_EXAMPLES=ON
cmake --build "$build_dir"
"$module_dir/tools/check-hxi.sh"

if [[ -x "$haxeon_dir/scripts/build-runtime.sh" ]]; then
	"$haxeon_dir/scripts/build-runtime.sh"
elif [[ -x "$haxeon_dir/scripts/build-native.sh" ]]; then
	(cd "$haxeon_dir" && ./scripts/build-native.sh)
else
	echo "test-haxeon: Haxeon runtime build script not found" >&2
	exit 2
fi

hashlink_runtime="$haxeon_dir/.tools/hashlink/hl"
if [[ ! -x "$hashlink_runtime" ]]; then
	hashlink_runtime="$haxeon_dir/vendor/hashlink/hl"
fi
if [[ ! -x "$hashlink_runtime" ]]; then
	echo "test-haxeon: missing HashLink runtime in .tools/hashlink or vendor/hashlink" >&2
	exit 2
fi

(cd "$haxeon_dir" && .tools/haxe/haxe -cp src --run compiler.tools.HaxeonCompiler \
    --output="$build_dir/haxeon-audio.hl" \
    --entry=AudioSmoke \
    --root="$module_dir/tests/haxeon" \
    --root="$module_dir/bindings/haxe" \
    --root="$nativekit_dir/bindings/haxe" \
    --ffi-interface="$nativekit_dir/bindings/haxe/nativekit.hxi" \
    --ffi-projection="$nativekit_dir/bindings/haxe/nativekit.hxmap" \
    --ffi-interface="$module_dir/bindings/nativekit-audio.hxi" \
    --ffi-projection="$module_dir/bindings/nativekit-audio.hxmap" \
    "$module_dir/tests/haxeon/AudioSmoke.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/AudioResult.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/Bus.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/Clip.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/Enums.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/Mixer.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/Voice.hx" \
    "$module_dir/bindings/haxe/nativekit/audio/VoiceOptions.hx" \
    "$nativekit_dir/bindings/haxe/nativekit/resource/Resource.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitError.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEvent.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventValue.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventContext.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventBytes.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEventDecoderTests.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitAudioEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWindowEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitInputEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitTextInput.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitServiceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitResourceEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRequestOutcome.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRequests.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitEvents.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitRuntime.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitWindow.hx" \
    "$nativekit_dir/bindings/haxe/NativeKitSurface.hx")

set +e
(cd "$haxeon_dir/out" && \
	LD_LIBRARY_PATH="$build_dir:$haxeon_dir/out:$haxeon_dir/.tools/hashlink:$haxeon_dir/vendor/hashlink${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$hashlink_runtime" "$build_dir/haxeon-audio.hl")
status=$?
set -e
if [[ $status -eq 77 ]]; then
	echo "SKIP: audio device is unavailable"
	exit 0
fi
if [[ $status -ne 0 ]]; then
	echo "test-haxeon: audio smoke test returned $status" >&2
	exit 1
fi
echo "PASS: Haxeon audio facade clocked, scheduled, faded, played, grouped, completed, and disposed audio resources"
