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

	private function new(kind:NativeKitAudio.DspEventKind, frameOffset:Int,
		instrument:Null<DspInstrument>, voiceId:Int, note:Int, velocity:Float,
		?parameter:DspParameter, value:Float = 0.0, oscillatorIndex:Int = 0) {
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
		return result;
	}
}
