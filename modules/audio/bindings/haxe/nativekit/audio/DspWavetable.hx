package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;
import haxe.io.Bytes;

/** Owns one immutable, band-limited native wavetable asset. */
class DspWavetable {
	static inline var MIN_SAMPLE_COUNT:Int = 32;
	static inline var MAX_SAMPLE_COUNT:Int = 4096;

	final value:NativeKitAudio.DspWavetableHandle;
	final owned:NativeKitAudio.OwnedDspWavetableHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedDspWavetableHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	/** Creates a wavetable from one periodic cycle of normalized samples. */
	public static function fromSamples(samples:Array<Float>):DspWavetable {
		if (samples == null)
			throw "DSP wavetable samples must not be null";
		var bytes = Bytes.alloc(samples.length * 4);
		for (index in 0...samples.length)
			bytes.setFloat(index * 4, samples[index]);
		return fromBytes(bytes, samples.length);
	}

	/** Creates a wavetable from native-endian float32 sample storage. */
	public static function fromBytes(samples:Bytes, sampleCount:Int):DspWavetable {
		if (samples == null)
			throw "DSP wavetable sample bytes must not be null";
		if (sampleCount < MIN_SAMPLE_COUNT || sampleCount > MAX_SAMPLE_COUNT || (sampleCount & (sampleCount - 1)) != 0)
			throw "DSP wavetable sample count must be a power of two between 32 and 4096";
		if (samples.length != sampleCount * 4)
			throw "DSP wavetable sample bytes must contain exactly one float32 per sample";
		var made = NativeKitAudio.nk_audio_dsp_wavetable_create(samples);
		AudioResult.check(made.status, "audio.dsp.wavetable.create");
		return new DspWavetable(made.out_wavetable);
	}

	public function nativeHandle():NativeKitAudio.DspWavetableHandle {
		ensureLive();
		return value;
	}

	/** Releases the source handle; patches already built from it remain valid. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.dsp.wavetable.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "DSP wavetable has been disposed";
	}
}
