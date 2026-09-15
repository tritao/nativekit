package nativekit.audio;

import NativeKitAudio;
import nativekit.audio.Enums.VoiceStealPolicy;

/** Configures voice admission for a bus and all of its child buses. */
class BusConcurrencyOptions {
	/** Maximum number of audible voices in the bus subtree; zero means unlimited. */
	public var maxVoices:Int = 0;
	/** Policy used to make room when maxVoices is reached. */
	public var stealPolicy:VoiceStealPolicy = VoiceStealPolicy.None;
	/** Keeps admitted voices advancing silently until capacity becomes available. */
	public var virtualize:Bool = false;

	public function new() {}

	@:allow(nativekit.audio.Bus)
	private function nativeValue():NativeKitAudio.NativeBusConcurrencyOptions {
		if (maxVoices < 0)
			throw "Audio bus maxVoices must be non-negative";
		var result = new NativeKitAudio.NativeBusConcurrencyOptions();
		result.set_struct_size(NativeKitAudio.NativeBusConcurrencyOptions.size());
		result.set_max_voices(maxVoices);
		result.set_steal_policy(stealPolicy);
		result.set_virtualize(virtualize);
		return result;
	}

	@:allow(nativekit.audio.Bus)
	private static function fromNative(value:NativeKitAudio.NativeBusConcurrencyOptions):BusConcurrencyOptions {
		var result = new BusConcurrencyOptions();
		result.maxVoices = value.get_max_voices();
		result.stealPolicy = value.get_steal_policy();
		result.virtualize = value.get_virtualize();
		return result;
	}
}
