package nativekit.audio;

import NativeKitAudio;

/** One typed source-to-destination modulation route in a DSP patch. */
class DspModulationRoute {
	public final source:DspModulationSource;
	public final destination:DspModulationDestination;
	public final amount:Float;
	public final polarity:DspModulationPolarity;
	/** One-based oscillator target; zero targets all oscillator sources. */
	public final oscillatorIndex:Int;

	public function new(source:DspModulationSource, destination:DspModulationDestination,
		amount:Float, ?polarity:DspModulationPolarity, oscillatorIndex:Int = 0) {
		this.source = source;
		this.destination = destination;
		this.amount = amount;
		this.polarity = polarity == null ? DspModulationPolarity.Bipolar : polarity;
		if (oscillatorIndex < 0 || oscillatorIndex > NativeKitAudioConstants.NK_AUDIO_DSP_MAX_OSCILLATORS)
			throw "DSP modulation oscillator target is invalid";
		if (oscillatorIndex != 0 && destination != DspModulationDestination.PitchSemitones &&
			destination != DspModulationDestination.OscillatorLevel &&
			destination != DspModulationDestination.OscillatorPhase)
			throw "DSP modulation oscillator target is only valid for oscillator destinations";
		this.oscillatorIndex = oscillatorIndex;
	}

	@:allow(nativekit.audio.DspPatchBuilder)
	private function nativeValue():NativeKitAudio.NativeDspModulationRouteOptions {
		var result = new NativeKitAudio.NativeDspModulationRouteOptions();
		result.set_struct_size(NativeKitAudio.NativeDspModulationRouteOptions.size());
		result.set_source(source);
		result.set_destination(destination);
		result.set_polarity(polarity);
		result.set_amount(amount);
		result.set_oscillator_index(oscillatorIndex);
		return result;
	}
}
