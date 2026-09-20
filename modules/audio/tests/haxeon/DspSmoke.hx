import NativeKitAudio;
import nativekit.audio.DspEngine;
import nativekit.audio.DspEngineOptions;
import nativekit.audio.DspEvent;
import nativekit.audio.DspOscillatorOptions;
import nativekit.audio.DspEnums.DspModulationDestination;
import nativekit.audio.DspEnums.DspModulationPolarity;
import nativekit.audio.DspEnums.DspModulationSource;
import nativekit.audio.DspEnums.DspParameter;
import nativekit.audio.DspEnums.DspWaveform;
import nativekit.audio.DspInstrument;
import nativekit.audio.DspPatch;
import nativekit.audio.DspRenderTarget;
import nativekit.audio.DspWavetable;

class DspSmoke {
	public static function run():Void {
		var engine:DspEngine = null;
		var patch:DspPatch = null;
		var instrument:DspInstrument = null;
		var wavetable:DspWavetable = null;
		try {
			var options = new DspEngineOptions();
			options.sampleRate = 48000;
			options.channels = 1;
			options.blockSize = 64;
			options.maxVoices = 4;
			engine = DspEngine.create(options);
			var samples:Array<Float> = [];
			for (index in 0...32)
				samples.push(Math.sin(2.0 * Math.PI * index / 32.0));
			wavetable = DspWavetable.fromSamples(samples);
			var builder = DspPatch.builder();
			builder.oscillators[0].wavetable = wavetable;
			builder.oscillators[0].phase = 0.125;
			var detuned = new DspOscillatorOptions();
			detuned.waveform = DspWaveform.Triangle;
			detuned.level = 0.25;
			detuned.detuneCents = 7.0;
			builder.addOscillator(detuned);
			builder.gain = 0.75;
			builder.lfo.rateHz = 5.0;
			builder.modulate(DspModulationSource.Lfo, DspModulationDestination.PitchSemitones, 2.0, DspModulationPolarity.Bipolar);
			builder.modulate(DspModulationSource.Lfo, DspModulationDestination.OscillatorLevel, 0.25, DspModulationPolarity.Unipolar, 2);
			builder.modulate(DspModulationSource.Envelope, DspModulationDestination.OscillatorPhase, 0.05, DspModulationPolarity.Bipolar, 1);
			builder.operatorModulate(1, DspModulationDestination.OscillatorFrequencyHz, 2, 100.0, DspModulationPolarity.Bipolar);
			builder.operatorModulate(1, DspModulationDestination.OscillatorPhase, 2, 0.05, DspModulationPolarity.Bipolar);
			builder.modulate(DspModulationSource.Envelope, DspModulationDestination.FilterCutoffHz, 400.0, DspModulationPolarity.Unipolar);
			patch = builder.build();
			instrument = patch.createInstrument(engine);
			wavetable.dispose();
			wavetable = null;
			instrument.setOscillatorParameter(1, DspParameter.OscillatorLevel, 0.2);
			if (Math.abs(instrument.oscillatorParameter(1, DspParameter.OscillatorLevel) - 0.2) > 0.0001)
				throw "Haxe DSP oscillator parameter did not round-trip";
			instrument.setParameter(DspParameter.Gain, 0.5);
			if (instrument.parameter(DspParameter.Gain) != 0.5)
				throw "Haxe DSP instrument parameter did not round-trip";
			var target = new DspRenderTarget(64, 1);
			engine.render(target, [
				DspEvent.noteOn(instrument, 7, 69),
				DspEvent.oscillatorParameter(instrument, 1, DspParameter.OscillatorPhase, 0.25, 16),
				DspEvent.parameter(instrument, DspParameter.Gain, 0.0, 32)
			]);
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
			if ((capabilities & NativeKitAudio.DspCapabilities.Wavetable) == 0
				|| (capabilities & NativeKitAudio.DspCapabilities.Lfo) == 0
					|| (capabilities & NativeKitAudio.DspCapabilities.Modulation) == 0)
				throw "Haxe DSP capabilities omitted modulation support";
		} catch (error:Dynamic) {
			if (instrument != null)
				instrument.dispose();
			if (patch != null)
				patch.dispose();
			if (wavetable != null)
				wavetable.dispose();
			if (engine != null)
				engine.dispose();
			throw error;
		}
		if (instrument != null)
			instrument.dispose();
		if (patch != null)
			patch.dispose();
		if (wavetable != null)
			wavetable.dispose();
		if (engine != null)
			engine.dispose();
	}
}
