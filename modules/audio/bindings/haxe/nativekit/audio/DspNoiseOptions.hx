package nativekit.audio;

import NativeKitAudio;

/** Configures the additive white-noise component of a DSP patch. */
class DspNoiseOptions {
	public var level:Float = 0.0;

	public function new() {}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspNoiseOptions {
		var result = new NativeKitAudio.NativeDspNoiseOptions();
		result.set_struct_size(NativeKitAudio.NativeDspNoiseOptions.size());
		result.set_level(level);
		return result;
	}
}
