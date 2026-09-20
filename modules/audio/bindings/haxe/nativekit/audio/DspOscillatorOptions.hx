package nativekit.audio;

import NativeKitAudio;

/** Configures the pitched oscillator component of a DSP patch. */
class DspOscillatorOptions {
	public var waveform:DspWaveform = DspWaveform.Sine;
	public var level:Float = 1.0;

	/** Optional immutable wavetable source; null selects the built-in waveform. */
	public var wavetable:DspWavetable = null;
	/** Relative tuning in cents; zero preserves the note frequency. */
	public var detuneCents:Float = 0.0;

	public function new() {}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspOscillatorOptions {
		var result = new NativeKitAudio.NativeDspOscillatorOptions();
		result.set_struct_size(NativeKitAudio.NativeDspOscillatorOptions.size());
		result.set_waveform(waveform);
		result.set_level(level);
		result.set_wavetable(wavetable == null ? NativeKitAudio.DspWavetableHandle.invalid() : wavetable.nativeHandle());
		result.set_detune_cents(detuneCents);
		return result;
	}
}
