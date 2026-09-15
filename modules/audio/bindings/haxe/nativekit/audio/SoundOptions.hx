package nativekit.audio;

import NativeKitAudio;
import nativekit.audio.Enums.SoundLoadFlags;

/** Configures how a sound is loaded and which mixer bus receives it. */
class SoundOptions {
	public var looping:Bool = false;
	public var streaming:Bool = false;
	public var asynchronous:Bool = false;
	public var bus:Null<Bus>;

	public function new(?bus:Bus) {
		this.bus = bus;
	}

	@:allow(nativekit.audio.Sound)
	@:allow(nativekit.audio.Voice)
	private function nativeValue():NativeKitAudio.NativeSoundOptions {
		var result = new NativeKitAudio.NativeSoundOptions();
		result.set_struct_size(NativeKitAudio.NativeSoundOptions.size());
		var flags:SoundLoadFlags = 0;
		if (looping)
			flags |= SoundLoadFlags.Looping;
		if (streaming)
			flags |= SoundLoadFlags.Streaming;
		if (asynchronous)
			flags |= SoundLoadFlags.Asynchronous;
		result.set_flags(flags);
		var busHandle = NativeKitAudio.BusHandle.invalid();
		if (bus != null)
			busHandle = bus.nativeHandle();
		result.set_bus(busHandle);
		return result;
	}
}
