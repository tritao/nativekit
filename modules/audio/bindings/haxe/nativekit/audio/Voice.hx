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

	/** Returns the loading state of this voice's source. */
	public function loadState():NativeKitAudio.VoiceLoadState {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_load_state(value);
		AudioResult.check(result.status, "audio.voice.loadState");
		return result.out_state;
	}

	/** Returns true once this voice's source has reached its playable readiness point. */
	public function isReady():Bool {
		return loadState() == NativeKitAudio.VoiceLoadState.Ready;
	}

	public function atEnd():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_at_end(value);
		AudioResult.check(result.status, "audio.voice.atEnd");
		return result.out_at_end;
	}

	/** Routes this stopped voice through a bus, or to the master endpoint. */
	public function setBus(bus:Null<Bus>):Void {
		ensureLive();
		var busHandle = NativeKitAudio.BusHandle.invalid();
		if (bus != null)
			busHandle = bus.nativeHandle();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_bus(value, busHandle),
			"audio.voice.setBus");
	}

	/** Sets the voice's concurrency priority; larger values are more important. */
	public function setPriority(priority:Int):Void {
		ensureLive();
		if (priority < 0)
			throw "Audio voice priority must be non-negative";
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_priority(value, priority),
			"audio.voice.setPriority");
	}

	/** Returns the voice's concurrency priority. */
	public function priority():Int {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_priority(value);
		AudioResult.check(result.status, "audio.voice.priority");
		return result.out_priority;
	}

	/** Returns whether this voice is advancing without an audible bus slot. */
	public function isVirtualized():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_is_virtualized(value);
		AudioResult.check(result.status, "audio.voice.isVirtualized");
		return result.out_virtualized;
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

	/** Enables or disables 3D spatialization for this voice. */
	public function setSpatializationEnabled(enabled:Bool):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_spatialization_enabled(value, enabled),
			"audio.voice.setSpatializationEnabled");
	}

	/** Returns whether 3D spatialization is enabled for this voice. */
	public function isSpatializationEnabled():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_is_spatialization_enabled(value);
		AudioResult.check(result.status, "audio.voice.isSpatializationEnabled");
		return result.out_enabled;
	}

	/** Sets this voice's position in world or listener-relative coordinates. */
	public function setPosition(position:Vector3):Void {
		ensureLive();
		if (position == null)
			throw "Audio voice position must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_position(value, position.nativeValue()),
			"audio.voice.setPosition");
	}

	/** Returns this voice's position. */
	public function position():Vector3 {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_position(value);
		AudioResult.check(result.status, "audio.voice.position");
		return Vector3.fromNative(result.out_position);
	}

	/** Sets this voice's forward direction. */
	public function setDirection(direction:Vector3):Void {
		ensureLive();
		if (direction == null)
			throw "Audio voice direction must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_direction(value, direction.nativeValue()),
			"audio.voice.setDirection");
	}

	/** Returns this voice's forward direction. */
	public function direction():Vector3 {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_direction(value);
		AudioResult.check(result.status, "audio.voice.direction");
		return Vector3.fromNative(result.out_direction);
	}

	/** Sets this voice's velocity in world units per second. */
	public function setVelocity(velocity:Vector3):Void {
		ensureLive();
		if (velocity == null)
			throw "Audio voice velocity must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_velocity(value, velocity.nativeValue()),
			"audio.voice.setVelocity");
	}

	/** Returns this voice's velocity in world units per second. */
	public function velocity():Vector3 {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_velocity(value);
		AudioResult.check(result.status, "audio.voice.velocity");
		return Vector3.fromNative(result.out_velocity);
	}

	/** Sets this voice's distance attenuation model. */
	public function setAttenuationModel(model:NativeKitAudio.AttenuationModel):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_attenuation_model(value, model),
			"audio.voice.setAttenuationModel");
	}

	/** Returns this voice's distance attenuation model. */
	public function attenuationModel():NativeKitAudio.AttenuationModel {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_attenuation_model(value);
		AudioResult.check(result.status, "audio.voice.attenuationModel");
		return result.out_model;
	}

	/** Sets whether this voice position is absolute or listener-relative. */
	public function setPositioning(positioning:NativeKitAudio.Positioning):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_positioning(value, positioning),
			"audio.voice.setPositioning");
	}

	/** Returns this voice's position interpretation. */
	public function positioning():NativeKitAudio.Positioning {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_positioning(value);
		AudioResult.check(result.status, "audio.voice.positioning");
		return result.out_positioning;
	}

	/** Sets the distance attenuation rolloff. */
	public function setRolloff(rolloff:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_rolloff(value, rolloff),
			"audio.voice.setRolloff");
	}

	/** Returns the distance attenuation rolloff. */
	public function rolloff():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_rolloff(value);
		AudioResult.check(result.status, "audio.voice.rolloff");
		return result.out_rolloff;
	}

	/** Sets the minimum and maximum gain applied by spatialization. */
	public function setGainLimits(minGain:Float, maxGain:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_gain_limits(value, minGain, maxGain),
			"audio.voice.setGainLimits");
	}

	/** Returns the minimum and maximum gain applied by spatialization. */
	public function gainLimits():GainLimits {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_gain_limits(value);
		AudioResult.check(result.status, "audio.voice.gainLimits");
		return new GainLimits(result.out_min_gain, result.out_max_gain);
	}

	/** Sets the minimum and maximum distances used by attenuation. */
	public function setDistanceLimits(minDistance:Float, maxDistance:Float):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_set_distance_limits(value, minDistance, maxDistance),
			"audio.voice.setDistanceLimits");
	}

	/** Returns the minimum and maximum distances used by attenuation. */
	public function distanceLimits():DistanceLimits {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_distance_limits(value);
		AudioResult.check(result.status, "audio.voice.distanceLimits");
		return new DistanceLimits(result.out_min_distance, result.out_max_distance);
	}

	/** Sets the Doppler multiplier; zero disables Doppler pitch shifting. */
	public function setDopplerFactor(factor:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_doppler_factor(value, factor),
			"audio.voice.setDopplerFactor");
	}

	/** Returns the Doppler multiplier. */
	public function dopplerFactor():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_doppler_factor(value);
		AudioResult.check(result.status, "audio.voice.dopplerFactor");
		return result.out_factor;
	}

	/** Sets this voice's directional attenuation cone. */
	public function setCone(cone:Cone):Void {
		ensureLive();
		if (cone == null)
			throw "Audio voice cone must not be null";
		AudioResult.check(NativeKitAudio.nk_audio_voice_set_cone(value, cone.innerAngleRadians,
			cone.outerAngleRadians, cone.outerGain), "audio.voice.setCone");
	}

	/** Returns this voice's directional attenuation cone. */
	public function cone():Cone {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_cone(value);
		AudioResult.check(result.status, "audio.voice.cone");
		return new Cone(result.out_inner_angle_radians, result.out_outer_angle_radians,
			result.out_outer_gain);
	}

	/** Sets how strongly source direction affects per-channel spatial gain. */
	public function setDirectionalAttenuationFactor(factor:Float):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_voice_set_directional_attenuation_factor(value, factor),
			"audio.voice.setDirectionalAttenuationFactor");
	}

	/** Returns how strongly source direction affects per-channel spatial gain. */
	public function directionalAttenuationFactor():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_voice_get_directional_attenuation_factor(value);
		AudioResult.check(result.status, "audio.voice.directionalAttenuationFactor");
		return result.out_factor;
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
