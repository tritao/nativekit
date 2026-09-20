package nativekit.audio;

import NativeKitAudio;
import haxe.io.Bytes;

/** Owns one interleaved float render block returned by a DSP engine. */
class DspRenderTarget {
	public final frameCount:Int;
	public final channels:Int;
	public var samples:Bytes;

	public function new(frameCount:Int, channels:Int) {
		if (frameCount <= 0)
			throw "DSP render frame count must be positive";
		if (channels <= 0)
			throw "DSP render channel count must be positive";
		this.frameCount = frameCount;
		this.channels = channels;
		this.samples = Bytes.alloc(frameCount * channels * 4);
	}

	@:allow(nativekit.audio.DspEngine)
	private function nativeValue():NativeKitAudio.NativeDspRenderTarget {
		var result = new NativeKitAudio.NativeDspRenderTarget();
		result.set_struct_size(NativeKitAudio.NativeDspRenderTarget.size());
		result.set_samples_bytes(samples);
		result.set_sample_count(haxe.Int64.ofInt(samples.length >> 2));
		result.set_frame_count(frameCount);
		result.set_channels(channels);
		return result;
	}
}
