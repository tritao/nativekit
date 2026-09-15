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

	/** Sets the process-wide listener position in world coordinates. */
	public static function setListenerPosition(position:Vector3):Void {
		if (position == null)
			throw "Audio listener position must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_position(position.nativeValue()),
			"audio.listener.setPosition");
	}

	/** Returns the process-wide listener position in world coordinates. */
	public static function listenerPosition():Vector3 {
		var result = NativeKitAudio.nk_audio_listener_get_position();
		AudioResult.check(result.status, "audio.listener.position");
		return Vector3.fromNative(result.out_position);
	}

	/** Sets the process-wide listener forward direction. */
	public static function setListenerDirection(direction:Vector3):Void {
		if (direction == null)
			throw "Audio listener direction must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_direction(direction.nativeValue()),
			"audio.listener.setDirection");
	}

	/** Returns the process-wide listener forward direction. */
	public static function listenerDirection():Vector3 {
		var result = NativeKitAudio.nk_audio_listener_get_direction();
		AudioResult.check(result.status, "audio.listener.direction");
		return Vector3.fromNative(result.out_direction);
	}

	/** Sets the process-wide listener velocity in world units per second. */
	public static function setListenerVelocity(velocity:Vector3):Void {
		if (velocity == null)
			throw "Audio listener velocity must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_velocity(velocity.nativeValue()),
			"audio.listener.setVelocity");
	}

	/** Returns the process-wide listener velocity in world units per second. */
	public static function listenerVelocity():Vector3 {
		var result = NativeKitAudio.nk_audio_listener_get_velocity();
		AudioResult.check(result.status, "audio.listener.velocity");
		return Vector3.fromNative(result.out_velocity);
	}

	/** Sets the process-wide listener world-up direction. */
	public static function setListenerWorldUp(worldUp:Vector3):Void {
		if (worldUp == null)
			throw "Audio listener world up must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_world_up(worldUp.nativeValue()),
			"audio.listener.setWorldUp");
	}

	/** Returns the process-wide listener world-up direction. */
	public static function listenerWorldUp():Vector3 {
		var result = NativeKitAudio.nk_audio_listener_get_world_up();
		AudioResult.check(result.status, "audio.listener.worldUp");
		return Vector3.fromNative(result.out_world_up);
	}

	/** Sets the process-wide listener directional attenuation cone. */
	public static function setListenerCone(cone:Cone):Void {
		if (cone == null)
			throw "Audio listener cone must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_cone(cone.innerAngleRadians,
			cone.outerAngleRadians, cone.outerGain), "audio.listener.setCone");
	}

	/** Returns the process-wide listener directional attenuation cone. */
	public static function listenerCone():Cone {
		var result = NativeKitAudio.nk_audio_listener_get_cone();
		AudioResult.check(result.status, "audio.listener.cone");
		return new Cone(result.out_inner_angle_radians, result.out_outer_angle_radians,
			result.out_outer_gain);
	}

	/** Sets the speed of sound used for Doppler calculations. */
	public static function setSpeedOfSound(speed:Float):Void
		AudioResult.check(NativeKitAudio.nk_audio_listener_set_speed_of_sound(speed),
			"audio.listener.setSpeedOfSound");

	/** Returns the speed of sound used for Doppler calculations. */
	public static function speedOfSound():Float {
		var result = NativeKitAudio.nk_audio_listener_get_speed_of_sound();
		AudioResult.check(result.status, "audio.listener.speedOfSound");
		return result.out_speed;
	}
}
