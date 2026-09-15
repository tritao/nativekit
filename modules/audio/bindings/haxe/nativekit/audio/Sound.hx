package nativekit.audio;

import NativeKitAudio;
import NativeKit;
import NativeKitError;
import haxe.io.Bytes;

/** A stopped or playing sound instance owned by NativeKit's audio mixer. */
class Sound {
	final value:NativeKitAudio.SoundHandle;
	final owned:NativeKitAudio.OwnedSoundHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedSoundHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function fromFile(path:String, ?options:SoundOptions):Sound {
		if (path == null || path.length == 0)
			throw "Audio sound path must not be empty";
		if (options == null)
			options = new SoundOptions();
		var made = NativeKitAudio.nk_audio_sound_create_from_file(path, options.nativeValue());
		AudioResult.check(made.status, "audio.sound.fromFile");
		return new Sound(made.out_sound);
	}

	public static function fromMemory(data:Bytes, ?options:SoundOptions):Sound {
		if (data == null || data.length == 0)
			throw "Audio sound data must not be empty";
		if (options == null)
			options = new SoundOptions();
		var made = NativeKitAudio.nk_audio_sound_create_from_memory(data, data.length,
			options.nativeValue());
		AudioResult.check(made.status, "audio.sound.fromMemory");
		return new Sound(made.out_sound);
	}

	public function nativeHandle():NativeKitAudio.SoundHandle {
		ensureLive();
		return value;
	}

	public function start():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_start(value), "audio.sound.start");
	}

	public function stop():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_stop(value), "audio.sound.stop");
	}

	public function rewind():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_rewind(value), "audio.sound.rewind");
	}

	public function isPlaying():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_is_playing(value);
		AudioResult.check(result.status, "audio.sound.isPlaying");
		return result.out_playing;
	}

	public function atEnd():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_at_end(value);
		AudioResult.check(result.status, "audio.sound.atEnd");
		return result.out_at_end;
	}

	public function setVolume(volume:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_set_volume(value, volume),
			"audio.sound.setVolume");
	}

	public function volume():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_get_volume(value);
		AudioResult.check(result.status, "audio.sound.volume");
		return result.out_volume;
	}

	public function setPan(pan:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_set_pan(value, pan), "audio.sound.setPan");
	}

	public function pan():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_get_pan(value);
		AudioResult.check(result.status, "audio.sound.pan");
		return result.out_pan;
	}

	public function setPitch(pitch:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_set_pitch(value, pitch),
			"audio.sound.setPitch");
	}

	public function pitch():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_get_pitch(value);
		AudioResult.check(result.status, "audio.sound.pitch");
		return result.out_pitch;
	}

	public function setLooping(looping:Bool):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_sound_set_looping(value, looping),
			"audio.sound.setLooping");
	}

	public function isLooping():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_is_looping(value);
		AudioResult.check(result.status, "audio.sound.isLooping");
		return result.out_looping;
	}

	public function timeSeconds():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_get_time_seconds(value);
		AudioResult.check(result.status, "audio.sound.timeSeconds");
		return result.out_seconds;
	}

	public function lengthSeconds():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_sound_get_length_seconds(value);
		AudioResult.check(result.status, "audio.sound.lengthSeconds");
		return result.out_seconds;
	}

	/** Releases the sound and invalidates its native handle. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.sound.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Audio sound has been disposed";
	}
}
