package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;

/** Engine-owned DSP instrument instance created from an immutable patch. */
class DspInstrument {
	final value:NativeKitAudio.DspInstrumentHandle;
	final owned:NativeKitAudio.OwnedDspInstrumentHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedDspInstrumentHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public function nativeHandle():NativeKitAudio.DspInstrumentHandle {
		ensureLive();
		return value;
	}

	/** Sets an instrument parameter between render calls. */
	public function setParameter(parameter:DspParameter, value:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_dsp_instrument_set_parameter(this.value,
			parameter, value), "audio.dsp.instrument.setParameter");
	}

	/** Sets one oscillator-specific parameter between render calls. */
	public function setOscillatorParameter(oscillatorIndex:Int, parameter:DspParameter,
		value:Float):Void {
		ensureLive();
		if (oscillatorIndex < 0)
			throw "DSP oscillator parameter index must not be negative";
		AudioResult.check(NativeKitAudio.nk_audio_dsp_instrument_set_oscillator_parameter(this.value,
			oscillatorIndex, parameter, value), "audio.dsp.instrument.setOscillatorParameter");
	}

	/** Reads the current value of an instrument parameter. */
	public function parameter(parameter:DspParameter):Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_dsp_instrument_get_parameter(this.value, parameter);
		AudioResult.check(result.status, "audio.dsp.instrument.parameter");
		return result.out_value;
	}

	/** Reads one oscillator-specific parameter. */
	public function oscillatorParameter(oscillatorIndex:Int, parameter:DspParameter):Float {
		ensureLive();
		if (oscillatorIndex < 0)
			throw "DSP oscillator parameter index must not be negative";
		var result = NativeKitAudio.nk_audio_dsp_instrument_get_oscillator_parameter(this.value,
			oscillatorIndex, parameter);
		AudioResult.check(result.status, "audio.dsp.instrument.oscillatorParameter");
		return result.out_value;
	}

	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.dsp.instrument.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "DSP instrument has been disposed";
	}
}
