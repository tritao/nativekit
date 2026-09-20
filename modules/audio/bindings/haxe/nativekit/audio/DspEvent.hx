package nativekit.audio;

import NativeKitAudio;

/** One sample-accurate operation applied during a DSP render block. */
class DspEvent {
	final kind:NativeKitAudio.DspEventKind;
	final frameOffset:Int;
	final instrument:Null<DspInstrument>;
	final voiceId:Int;
	final note:Int;
	final velocity:Float;
	final parameterValue:Null<DspParameter>;
	final oscillatorIndex:Int;
	final value:Float;
	final endValue:Float;
	final durationFrames:Int;

	private function new(kind:NativeKitAudio.DspEventKind, frameOffset:Int,
		instrument:Null<DspInstrument>, voiceId:Int, note:Int, velocity:Float,
		?parameter:DspParameter, value:Float = 0.0, oscillatorIndex:Int = 0,
		endValue:Float = 0.0, durationFrames:Int = 0) {
		if (frameOffset < 0)
			throw "DSP event frame offset must not be negative";
		this.kind = kind;
		this.frameOffset = frameOffset;
		this.instrument = instrument;
		this.voiceId = voiceId;
		this.note = note;
		this.velocity = velocity;
		this.parameterValue = parameter;
		this.oscillatorIndex = oscillatorIndex;
		this.value = value;
		this.endValue = endValue;
		this.durationFrames = durationFrames;
	}

	/** Starts a voice at an inclusive MIDI note and normalized velocity. */
	public static function noteOn(instrument:DspInstrument, voiceId:Int, note:Int,
		velocity:Float = 1.0, frameOffset:Int = 0):DspEvent {
		if (instrument == null)
			throw "DSP note-on instrument must not be null";
		return new DspEvent(NativeKitAudio.DspEventKind.NoteOn, frameOffset, instrument,
			voiceId, note, velocity);
	}

	/** Releases a voice identity at an exact frame offset. */
	public static function noteOff(voiceId:Int, frameOffset:Int = 0):DspEvent
		return new DspEvent(NativeKitAudio.DspEventKind.NoteOff, frameOffset, null,
			voiceId, 0, 0.0);

	/** Changes an instrument parameter at an exact frame offset. */
	public static function parameter(instrument:DspInstrument, parameter:DspParameter,
		value:Float, frameOffset:Int = 0):DspEvent {
		if (instrument == null)
			throw "DSP parameter instrument must not be null";
		if (parameter >= DspParameter.OscillatorWaveform)
			throw "DSP oscillator parameters require oscillatorParameter()";
		return new DspEvent(NativeKitAudio.DspEventKind.Parameter, frameOffset, instrument,
			0, 0, 0.0, parameter, value);
	}

	/** Changes one oscillator-specific parameter at an exact frame offset. */
	public static function oscillatorParameter(instrument:DspInstrument, oscillatorIndex:Int,
		parameter:DspParameter, value:Float, frameOffset:Int = 0):DspEvent {
		if (instrument == null)
			throw "DSP oscillator parameter instrument must not be null";
		if (oscillatorIndex < 0)
			throw "DSP oscillator parameter index must not be negative";
		if (parameter < DspParameter.OscillatorWaveform || parameter > DspParameter.OscillatorPhase)
			throw "DSP oscillator parameter kind is invalid";
		return new DspEvent(NativeKitAudio.DspEventKind.Parameter, frameOffset, instrument,
			0, 0, 0.0, parameter, value, oscillatorIndex);
	}

	/** Linearly changes a global instrument parameter over exact audio frames. */
	public static function parameterRamp(instrument:DspInstrument, parameter:DspParameter,
		startValue:Float, endValue:Float, durationFrames:Int, frameOffset:Int = 0):DspEvent {
		if (instrument == null)
			throw "DSP parameter ramp instrument must not be null";
		if (parameter >= DspParameter.OscillatorWaveform)
			throw "DSP oscillator parameters require oscillatorParameterRamp()";
		validateRampDuration(durationFrames);
		if (parameter == DspParameter.Waveform)
			throw "DSP parameter ramp cannot target a waveform";
		return new DspEvent(NativeKitAudio.DspEventKind.ParameterRamp, frameOffset, instrument,
			0, 0, 0.0, parameter, startValue, 0, endValue, durationFrames);
	}

	/** Linearly changes one oscillator-specific parameter over exact audio frames. */
	public static function oscillatorParameterRamp(instrument:DspInstrument, oscillatorIndex:Int,
		parameter:DspParameter, startValue:Float, endValue:Float, durationFrames:Int,
		frameOffset:Int = 0):DspEvent {
		if (instrument == null)
			throw "DSP oscillator parameter ramp instrument must not be null";
		if (oscillatorIndex < 0)
			throw "DSP oscillator parameter ramp index must not be negative";
		if (parameter < DspParameter.OscillatorWaveform || parameter > DspParameter.OscillatorPhase)
			throw "DSP oscillator parameter ramp kind is invalid";
		if (parameter == DspParameter.OscillatorWaveform)
			throw "DSP parameter ramp cannot target a waveform";
		validateRampDuration(durationFrames);
		return new DspEvent(NativeKitAudio.DspEventKind.ParameterRamp, frameOffset, instrument,
			0, 0, 0.0, parameter, startValue, oscillatorIndex, endValue, durationFrames);
	}

	static function validateRampDuration(durationFrames:Int):Void {
		if (durationFrames <= 0)
			throw "DSP parameter ramp duration must be positive";
	}

	@:allow(nativekit.audio.DspEngine)
	private function nativeValue():NativeKitAudio.NativeDspEvent {
		var result = new NativeKitAudio.NativeDspEvent();
		result.set_struct_size(NativeKitAudio.NativeDspEvent.size());
		result.set_kind(kind);
		result.set_frame_offset(frameOffset);
		result.set_instrument(instrument == null
			? NativeKitAudio.DspInstrumentHandle.invalid()
			: instrument.nativeHandle());
		result.set_voice_id(voiceId);
		result.set_note(note);
		result.set_velocity(velocity);
		result.set_parameter(parameterValue == null ? DspParameter.Gain : parameterValue);
		result.set_value(value);
		result.set_oscillator_index(oscillatorIndex);
		result.set_end_value(endValue);
		result.set_duration_frames(durationFrames);
		return result;
	}
}
