package nativekit.audio;

import NativeKitAudio;
import nativekit.audio.Enums.VoiceLoadFlags;

/** Configures a voice's primitive playback behavior. */
class VoiceOptions {
	public var looping:Bool = false;
	public var streaming:Bool = false;
	public var asynchronous:Bool = false;

	public function new() {}

	@:allow(nativekit.audio.Voice)
	private function nativeValue():NativeKitAudio.NativeVoiceOptions {
		var result = new NativeKitAudio.NativeVoiceOptions();
		result.set_struct_size(NativeKitAudio.NativeVoiceOptions.size());
		var flags:VoiceLoadFlags = 0;
		if (looping)
			flags |= VoiceLoadFlags.Looping;
		if (streaming)
			flags |= VoiceLoadFlags.Streaming;
		if (asynchronous)
			flags |= VoiceLoadFlags.Asynchronous;
		result.set_flags(flags);
		return result;
	}
}
