package nativekit.audio;

import NativeKitAudio;
import nativekit.audio.Enums.VoiceLoadFlags;

/** Configures a voice's playback behavior and mixer routing. */
class VoiceOptions {
	public var looping:Bool = false;
	public var streaming:Bool = false;
	public var asynchronous:Bool = false;
	/** Larger values win when a bus must steal or reject a voice. */
	public var priority:Int = 0;
	public var bus:Null<Bus>;

	public function new(?bus:Bus) {
		this.bus = bus;
	}

	@:allow(nativekit.audio.Voice)
	private function nativeValue():NativeKitAudio.NativeVoiceOptions {
		if (priority < 0)
			throw "Audio voice priority must be non-negative";
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
		result.set_priority(priority);
		var busHandle = NativeKitAudio.BusHandle.invalid();
		if (bus != null)
			busHandle = bus.nativeHandle();
		result.set_bus(busHandle);
		return result;
	}
}
