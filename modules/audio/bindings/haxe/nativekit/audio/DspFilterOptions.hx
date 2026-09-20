package nativekit.audio;

import NativeKitAudio;

/** Configures the optional filter component of a DSP patch. */
class DspFilterOptions {
	public var type:DspFilterType = DspFilterType.None;
	public var cutoffHz:Float = 0.0;
	public var resonance:Float = 0.0;

	public function new() {}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspFilterOptions {
		var result = new NativeKitAudio.NativeDspFilterOptions();
		result.set_struct_size(NativeKitAudio.NativeDspFilterOptions.size());
		result.set_type(type);
		result.set_cutoff_hz(cutoffHz);
		result.set_resonance(resonance);
		return result;
	}
}
