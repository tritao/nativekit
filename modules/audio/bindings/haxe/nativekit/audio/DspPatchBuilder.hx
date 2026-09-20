package nativekit.audio;

import NativeKitAudio;

/** Mutable Haxe-side builder for an immutable native DSP patch. */
class DspPatchBuilder {
	/** Pitched sources are mixed additively in source order. */
	public final oscillators:Array<DspOscillatorOptions> = [new DspOscillatorOptions()];
	public var noise:DspNoiseOptions = new DspNoiseOptions();
	public var envelope:DspEnvelopeOptions = new DspEnvelopeOptions();
	public var filter:DspFilterOptions = new DspFilterOptions();
	public var lfo:DspLfoOptions = new DspLfoOptions();
	public var gain:Float = 1.0;
	public final routes:Array<DspModulationRoute> = [];

	public function new() {}

	/** Appends a pitched source and returns this builder for fluent construction. */
	public function addOscillator(?oscillator:DspOscillatorOptions):DspPatchBuilder {
		if (oscillators.length >= NativeKitAudioConstants.NK_AUDIO_DSP_MAX_OSCILLATORS)
			throw "DSP patch oscillator limit exceeded";
		oscillators.push(oscillator == null ? new DspOscillatorOptions() : oscillator);
		return this;
	}

	/** Appends a route and returns this builder for fluent patch construction. */
	public function addRoute(route:DspModulationRoute):DspPatchBuilder {
		if (route == null)
			throw "DSP modulation route must not be null";
		if (routes.length >= NativeKitAudioConstants.NK_AUDIO_DSP_MAX_MODULATION_ROUTES)
			throw "DSP patch modulation route limit exceeded";
		routes.push(route);
		return this;
	}

	/** Convenience form of addRoute(). */
	public function modulate(source:DspModulationSource,
		destination:DspModulationDestination, amount:Float,
		?polarity:DspModulationPolarity, oscillatorIndex:Int = 0):DspPatchBuilder {
		return addRoute(new DspModulationRoute(source, destination, amount, polarity, oscillatorIndex));
	}

	/** Materializes the current builder state into an immutable native patch. */
	public function build():DspPatch {
		if (oscillators == null || noise == null || envelope == null || filter == null || lfo == null)
			throw "DSP patch components must not be null";
		if (oscillators.length > NativeKitAudioConstants.NK_AUDIO_DSP_MAX_OSCILLATORS)
			throw "DSP patch oscillator limit exceeded";
		var options = new NativeKitAudio.NativeDspPatchOptions();
		options.set_struct_size(NativeKitAudio.NativeDspPatchOptions.size());
		options.set_oscillator_count(oscillators.length);
		for (index in 0...oscillators.length) {
			if (oscillators[index] == null)
				throw "DSP patch oscillators must not be null";
			options.set_oscillators(index, oscillators[index].nativeValue());
		}
		options.set_noise(noise.nativeValue());
		options.set_envelope(envelope.nativeValue());
		options.set_filter(filter.nativeValue());
		options.set_gain(gain);
		options.set_lfo(lfo.nativeValue());
		for (index in 0...routes.length)
			options.set_routes(index, routes[index].nativeValue());
		options.set_route_count(routes.length);
		var made = NativeKitAudio.nk_audio_dsp_patch_create(options);
		AudioResult.check(made.status, "audio.dsp.patch.build");
		return new DspPatch(made.out_patch);
	}
}
