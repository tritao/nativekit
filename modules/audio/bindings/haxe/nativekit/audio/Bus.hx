package nativekit.audio;

import NativeKitAudio;
import NativeKit;
import NativeKitError;

/** A mixer bus that groups sounds under shared volume and transport controls. */
class Bus {
	/** Passed as fade's starting volume to use the current fader volume. */
	public static inline var CURRENT_VOLUME:Float = -1.0;

	final value:NativeKitAudio.BusHandle;
	final owned:NativeKitAudio.OwnedBusHandle;
	final effects:Array<BusEffect> = [];
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedBusHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function create(?parent:Bus):Bus {
		var options = new NativeKitAudio.NativeBusOptions();
		options.set_struct_size(NativeKitAudio.NativeBusOptions.size());
		var parentHandle = NativeKitAudio.BusHandle.invalid();
		if (parent != null)
			parentHandle = parent.nativeHandle();
		options.set_parent(parentHandle);
		var made = NativeKitAudio.nk_audio_bus_create(options);
		AudioResult.check(made.status, "audio.bus.create");
		return new Bus(made.out_bus);
	}

	public function nativeHandle():NativeKitAudio.BusHandle {
		ensureLive();
		return value;
	}

	public function start():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_bus_start(value), "audio.bus.start");
	}

	public function stop():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_bus_stop(value), "audio.bus.stop");
	}

	/** Routes this bus through a parent bus, or directly to the master endpoint. */
	public function setParent(parent:Null<Bus>):Void {
		ensureLive();
		var parentHandle = NativeKitAudio.BusHandle.invalid();
		if (parent != null)
			parentHandle = parent.nativeHandle();
		AudioResult.check(NativeKitAudio.nk_audio_bus_set_parent(value, parentHandle),
			"audio.bus.setParent");
	}

	/** Returns whether this bus is routed through a live parent bus handle. */
	public function hasParent():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_bus_get_parent(value);
		AudioResult.check(result.status, "audio.bus.hasParent");
		return result.out_parent.rawValue() != 0;
	}

	/** Schedules the bus to start at an absolute process-wide audio time in PCM frames. */
	public function scheduleStart(timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_bus_schedule_start(value, timeFrames),
			"audio.bus.scheduleStart");
	}

	/** Schedules the bus to stop at an absolute process-wide audio time in PCM frames. */
	public function scheduleStop(timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_bus_schedule_stop(value, timeFrames),
			"audio.bus.scheduleStop");
	}

	/** Clears scheduled start, stop, and fade transitions. */
	public function clearSchedule():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_bus_clear_schedule(value),
			"audio.bus.clearSchedule");
	}

	public function isPlaying():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_bus_is_playing(value);
		AudioResult.check(result.status, "audio.bus.isPlaying");
		return result.out_playing;
	}

	public function setVolume(volume:Float):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_bus_set_volume(value, volume),
			"audio.bus.setVolume");
	}

	public function volume():Float {
		ensureLive();
		var result = NativeKitAudio.nk_audio_bus_get_volume(value);
		AudioResult.check(result.status, "audio.bus.volume");
		return result.out_volume;
	}

	/** Fades the bus between linear gains over a duration in PCM frames. */
	public function fade(volumeBegin:Float, volumeEnd:Float, durationFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_bus_fade(value, volumeBegin, volumeEnd, durationFrames),
			"audio.bus.fade");
	}

	/** Fades the bus at an absolute process-wide audio time in PCM frames. */
	public function fadeAt(volumeBegin:Float, volumeEnd:Float, durationFrames:haxe.Int64,
		timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_bus_fade_at(value, volumeBegin, volumeEnd, durationFrames,
				timeFrames),
			"audio.bus.fadeAt");
	}

	public function setMuted(muted:Bool):Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_bus_set_muted(value, muted),
			"audio.bus.setMuted");
	}

	public function isMuted():Bool {
		ensureLive();
		var result = NativeKitAudio.nk_audio_bus_is_muted(value);
		AudioResult.check(result.status, "audio.bus.isMuted");
		return result.out_muted;
	}

	/** Appends a low-pass filter to this bus's effect chain. */
	public function addLowPass(cutoffFrequencyHz:Float, ?order:Int):BusEffect {
		ensureLive();
		var actualOrder = order == null ? 2 : order;
		var made = NativeKitAudio.nk_audio_bus_effect_create_low_pass(value, cutoffFrequencyHz,
			actualOrder);
		AudioResult.check(made.status, "audio.bus.addLowPass");
		var effect = new BusEffect(made.out_effect);
		effects.push(effect);
		return effect;
	}

	/** Appends a high-pass filter to this bus's effect chain. */
	public function addHighPass(cutoffFrequencyHz:Float, ?order:Int):BusEffect {
		ensureLive();
		var actualOrder = order == null ? 2 : order;
		var made = NativeKitAudio.nk_audio_bus_effect_create_high_pass(value, cutoffFrequencyHz,
			actualOrder);
		AudioResult.check(made.status, "audio.bus.addHighPass");
		var effect = new BusEffect(made.out_effect);
		effects.push(effect);
		return effect;
	}

	/** Appends a feedback delay to this bus's effect chain. */
	public function addDelay(delayFrames:Int, decay:Float):BusEffect {
		ensureLive();
		var made = NativeKitAudio.nk_audio_bus_effect_create_delay(value, delayFrames, decay);
		AudioResult.check(made.status, "audio.bus.addDelay");
		var effect = new BusEffect(made.out_effect);
		effects.push(effect);
		return effect;
	}

	/** Releases the bus; sounds already routed through it retain native ownership. */
	public function dispose():Void {
		if (disposed)
			return;
		for (effect in effects)
			effect.dispose();
		effects.resize(0);
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.bus.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Audio bus has been disposed";
	}
}
