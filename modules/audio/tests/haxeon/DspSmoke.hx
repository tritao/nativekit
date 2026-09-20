import NativeKitAudio;
import nativekit.audio.DspEngine;
import nativekit.audio.DspEngineOptions;
import nativekit.audio.DspEvent;
import nativekit.audio.DspEnums.DspModulationDestination;
import nativekit.audio.DspEnums.DspModulationPolarity;
import nativekit.audio.DspEnums.DspModulationSource;
import nativekit.audio.DspEnums.DspParameter;
import nativekit.audio.DspInstrument;
import nativekit.audio.DspPatch;
import nativekit.audio.DspRenderTarget;

class DspSmoke {
	public static function run():Void {
		var engine:DspEngine = null;
		var patch:DspPatch = null;
		var instrument:DspInstrument = null;
		try {
			var options = new DspEngineOptions();
			options.sampleRate = 48000;
			options.channels = 1;
			options.blockSize = 64;
			options.maxVoices = 4;
			engine = DspEngine.create(options);
			var builder = DspPatch.builder();
			builder.gain = 0.75;
			builder.lfo.rateHz = 5.0;
			builder.modulate(DspModulationSource.Lfo,
				DspModulationDestination.PitchSemitones, 2.0,
				DspModulationPolarity.Bipolar);
			builder.modulate(DspModulationSource.Envelope,
				DspModulationDestination.FilterCutoffHz, 400.0,
				DspModulationPolarity.Unipolar);
			patch = builder.build();
			instrument = patch.createInstrument(engine);
			instrument.setParameter(DspParameter.Gain, 0.5);
			if (instrument.parameter(DspParameter.Gain) != 0.5)
				throw "Haxe DSP instrument parameter did not round-trip";
			var target = new DspRenderTarget(64, 1);
			engine.render(target, [DspEvent.noteOn(instrument, 7, 69),
				DspEvent.parameter(instrument, DspParameter.Gain, 0.0, 32)]);
			var renderedSamples = target.samples;
			var containsSignal = false;
			for (index in 0...32)
				if (renderedSamples.getInt32(index * 4) != 0)
					containsSignal = true;
			if (!containsSignal)
				throw "Haxe DSP render did not produce a signal";
			if (renderedSamples.getInt32(48 * 4) != 0)
				throw "Haxe DSP parameter event did not apply at its frame offset";
			engine.render(target, [DspEvent.noteOff(7)]);
			var capabilities = engine.capabilities();
			if ((capabilities & NativeKitAudio.DspCapabilities.Lfo) == 0 ||
				(capabilities & NativeKitAudio.DspCapabilities.Modulation) == 0)
				throw "Haxe DSP capabilities omitted modulation support";
		} catch (error:Dynamic) {
			if (instrument != null)
				instrument.dispose();
			if (patch != null)
				patch.dispose();
			if (engine != null)
				engine.dispose();
			throw error;
		}
		if (instrument != null)
			instrument.dispose();
		if (patch != null)
			patch.dispose();
		if (engine != null)
			engine.dispose();
	}
}
