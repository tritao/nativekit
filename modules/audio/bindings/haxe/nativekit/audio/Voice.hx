package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;

/** One independent playback instance created from a reusable audio clip. */
class Voice {
	/** Passed as fade's starting volume to use the current fader volume. */
	public static inline var CURRENT_VOLUME:Float = -1.0;

	final value:NativeKitAudio.VoiceHandle;
	final owned:NativeKitAudio.OwnedVoiceHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedVoiceHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function fromClip(clip:Clip, ?options:VoiceOptions):Voice {
		if (clip == null)
			throw "Audio voice clip must not be null";
		if (options == null)
			options = new VoiceOptions();
		var made = NativeKitAudio.nk_audio_voice_create(clip.nativeHandle(), options.nativeValue());
		AudioResult.check(made.status, "audio.voice.create");
		return new Voice(made.out_voice);
	}

	public function nativeHandle():NativeKitAudio.VoiceHandle {
		ensureLive();
		return value;
	}

	public function start():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_start(value), "audio.voice.start");
	}

	public function stop():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_stop(value), "audio.voice.stop");
	}

	public function rewind():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_rewind(value), "audio.voice.rewind");
	}

	/** Schedules playback at an absolute process-wide audio time in PCM frames. */
	public function scheduleStart(timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_schedule_start(value, timeFrames),
			"audio.voice.scheduleStart");
	}

	/** Schedules playback to stop at an absolute process-wide audio time in PCM frames. */
	public function scheduleStop(timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_schedule_stop(value, timeFrames),
			"audio.voice.scheduleStop");
	}

	/** Clears scheduled start, stop, and fade transitions. */
	public function clearSchedule():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_clear_schedule(value),
			"audio.voice.clearSchedule");
	}

	/** Fades between linear gains over a duration in PCM frames. */
	public function fade(volumeBegin:Float, volumeEnd:Float, durationFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_fade(value, volumeBegin, volumeEnd, durationFrames),
			"audio.voice.fade");
	}

	/** Fades between linear gains at an absolute process-wide audio time in PCM frames. */
	public function fadeAt(volumeBegin:Float, volumeEnd:Float, durationFrames:haxe.Int64,
		timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_fade_at(value, volumeBegin, volumeEnd, durationFrames,
				timeFrames),
			"audio.voice.fadeAt");
	}

	public function isPlaying():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_is_playing(value);
		AudioResult.check(result.status, "audio.voice.isPlaying");
		return result.out_playing;
	}

	public function atEnd():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_at_end(value);
		AudioResult.check(result.status, "audio.voice.atEnd");
		return result.out_at_end;
	}

	public function setVolume(volume:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_volume(value, volume),
			"audio.voice.setVolume");
	}

	public function volume():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_volume(value);
		AudioResult.check(result.status, "audio.voice.volume");
		return result.out_volume;
	}

	public function setPan(pan:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_pan(value, pan), "audio.voice.setPan");
	}

	public function pan():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_pan(value);
		AudioResult.check(result.status, "audio.voice.pan");
		return result.out_pan;
	}

	public function setPitch(pitch:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_pitch(value, pitch),
			"audio.voice.setPitch");
	}

	public function pitch():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_pitch(value);
		AudioResult.check(result.status, "audio.voice.pitch");
		return result.out_pitch;
	}

	public function setLooping(looping:Bool):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_looping(value, looping),
			"audio.voice.setLooping");
	}

	public function isLooping():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_is_looping(value);
		AudioResult.check(result.status, "audio.voice.isLooping");
		return result.out_looping;
	}

	public function timeSeconds():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_time_seconds(value);
		AudioResult.check(result.status, "audio.voice.timeSeconds");
		return result.out_seconds;
	}

	public function lengthSeconds():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_length_seconds(value);
		AudioResult.check(result.status, "audio.voice.lengthSeconds");
		return result.out_seconds;
	}

	/** Releases the voice and invalidates its native handle. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.voice.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Audio voice has been disposed";
	}
}
