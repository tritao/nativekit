package nativekit.audio;

import NativeKitAudio;

/** Configures one standalone, device-independent DSP renderer. */
class DspEngineOptions {
	public var sampleRate:Int = 0;
	public var channels:Int = 0;
	public var blockSize:Int = 0;
	public var maxVoices:Int = 0;

	public function new() {}

	@:allow(nativekit.audio.DspEngine)
	private function nativeValue():NativeKitAudio.NativeDspEngineOptions {
		var result = new NativeKitAudio.NativeDspEngineOptions();
		result.set_struct_size(NativeKitAudio.NativeDspEngineOptions.size());
		result.set_sample_rate(sampleRate);
		result.set_channels(channels);
		result.set_block_size(blockSize);
		result.set_max_voices(maxVoices);
		return result;
	}
}
