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

	/** Reads the current value of an instrument parameter. */
	public function parameter(parameter:DspParameter):Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_dsp_instrument_get_parameter(this.value, parameter);
		AudioResult.check(result.status, "audio.dsp.instrument.parameter");
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
