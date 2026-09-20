package nativekit.audio;

import NativeKitAudio;

/** Configures the pitched oscillator component of a DSP patch. */
class DspOscillatorOptions {
	public var waveform:DspWaveform = DspWaveform.Sine;
	public var level:Float = 1.0;

	public function new() {}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspOscillatorOptions {
		var result = new NativeKitAudio.NativeDspOscillatorOptions();
		result.set_struct_size(NativeKitAudio.NativeDspOscillatorOptions.size());
		result.set_waveform(waveform);
		result.set_level(level);
		return result;
	}
}
