package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;
import haxe.io.Bytes;

/** Owns a standalone native DSP renderer and its instruments. */
class DspEngine {
	final value:NativeKitAudio.DspEngineHandle;
	final owned:NativeKitAudio.OwnedDspEngineHandle;
	final instruments:Array<DspInstrument> = [];
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedDspEngineHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function create(?options:DspEngineOptions):DspEngine {
		var actual = options == null ? new DspEngineOptions() : options;
		var made = NativeKitAudio.nk_audio_dsp_engine_create(actual.nativeValue());
		AudioResult.check(made.status, "audio.dsp.engine.create");
		return new DspEngine(made.out_engine);
	}

	public function nativeHandle():NativeKitAudio.DspEngineHandle {
		ensureLive();
		return value;
	}

	/** Creates an instrument that copies the supplied immutable patch. */
	public function createInstrument(patch:DspPatch):DspInstrument {
		ensureLive();
		if (patch == null)
			throw "DSP patch must not be null";
		var made = NativeKitAudio.nk_audio_dsp_instrument_create_from_patch(value,
			patch.nativeHandle());
		AudioResult.check(made.status, "audio.dsp.instrument.create");
		var result = new DspInstrument(made.out_instrument);
		instruments.push(result);
		return result;
	}

	public function reset():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_dsp_engine_reset(value), "audio.dsp.engine.reset");
	}

	public function capabilities():NativeKitAudio.DspCapabilities {
		ensureLive();
		var result = NativeKitAudio.nk_audio_dsp_engine_get_capabilities(value);
		AudioResult.check(result.status, "audio.dsp.engine.capabilities");
		return result.out_capabilities;
	}

	/** Renders one block into the supplied target and applies sorted events. */
	public function render(target:DspRenderTarget, ?events:Array<DspEvent>):Bytes {
		ensureLive();
		if (target == null)
			throw "DSP render target must not be null";
		var nativeEvents:Array<NativeKitAudio.NativeDspEvent> = [];
		if (events != null) {
			var previousFrame = 0;
			for (event in events) {
				if (event == null)
					throw "DSP render event must not be null";
				if (event.frameOffset < previousFrame)
					throw "DSP render events must be sorted by frame offset";
				previousFrame = event.frameOffset;
				nativeEvents.push(event.nativeValue());
			}
		}
		var nativeTarget = target.nativeValue();
		var rendered = NativeKitAudio.nk_audio_dsp_engine_render(value, nativeTarget, nativeEvents);
		AudioResult.check(rendered.status, "audio.dsp.engine.render");
		return target.samples;
	}

	public function dispose():Void {
		if (disposed)
			return;
		var failure:Dynamic = null;
		for (index in 0...instruments.length) {
			var instrument = instruments[instruments.length - 1 - index];
			try {
				instrument.dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
		instruments.resize(0);
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.dsp.engine.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "DSP engine has been disposed";
	}
}
