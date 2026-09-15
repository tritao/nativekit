package nativekit.audio;

import NativeKitAudio;

/** Process-wide NativeKit audio mixer controls. */
class Mixer {
	/** Returns the process-wide audio engine clock in PCM frames. */
	public static function timeFrames():haxe.Int64 {
		var result = NativeKitAudio.nk_audio_get_time_pcm_frames();
		AudioResult.check(result.status, "audio.mixer.timeFrames");
		return result.out_time_pcm_frames;
	}

	/** Returns the process-wide audio engine sample rate in frames per second. */
	public static function sampleRate():Int {
		var result = NativeKitAudio.nk_audio_get_sample_rate();
		AudioResult.check(result.status, "audio.mixer.sampleRate");
		return result.out_sample_rate;
	}

	public static function setMasterVolume(volume:Float):Void
		AudioResult.check(NativeKitAudio.nk_audio_set_master_volume(volume),
			"audio.mixer.setMasterVolume");

	public static function masterVolume():Float {
		var result = NativeKitAudio.nk_audio_get_master_volume();
		AudioResult.check(result.status, "audio.mixer.masterVolume");
		return result.out_volume;
	}
}
