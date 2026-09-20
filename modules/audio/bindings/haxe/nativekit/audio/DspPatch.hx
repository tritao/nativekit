package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;

/** Immutable reusable native DSP patch definition. */
class DspPatch {
	final value:NativeKitAudio.DspPatchHandle;
	final owned:NativeKitAudio.OwnedDspPatchHandle;
	var disposed:Bool = false;

	@:allow(nativekit.audio.DspPatchBuilder)
	private function new(owned:NativeKitAudio.OwnedDspPatchHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	/** Starts a patch builder with the native DSP defaults. */
	public static function builder():DspPatchBuilder
		return new DspPatchBuilder();

	public function nativeHandle():NativeKitAudio.DspPatchHandle {
		ensureLive();
		return value;
	}

	/** Creates an engine-owned instrument that copies this patch definition. */
	public function createInstrument(engine:DspEngine):DspInstrument {
		ensureLive();
		if (engine == null)
			throw "DSP patch engine must not be null";
		return engine.createInstrument(this);
	}

	/** Releases this patch; existing instruments retain their native copy. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.dsp.patch.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "DSP patch has been disposed";
	}
}
