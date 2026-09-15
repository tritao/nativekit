package nativekit.audio;

import NativeKitAudio;

/** Process-wide NativeKit audio mixer controls. */
class Mixer {
	public static function setMasterVolume(volume:Float):Void
		AudioResult.check(NativeKitAudio.nk_audio_set_master_volume(volume),
			"audio.mixer.setMasterVolume");

	public static function masterVolume():Float {
		var result = NativeKitAudio.nk_audio_get_master_volume();
		AudioResult.check(result.status, "audio.mixer.masterVolume");
		return result.out_volume;
	}
}
