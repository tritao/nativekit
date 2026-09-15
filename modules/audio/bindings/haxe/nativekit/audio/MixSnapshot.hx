package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;

/** Reusable bus mix targets for mode changes and dialogue ducking. */
class MixSnapshot {
	final value:NativeKitAudio.MixSnapshotHandle;
	final owned:NativeKitAudio.OwnedMixSnapshotHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedMixSnapshotHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	/** Creates an empty snapshot. */
	public static function create():MixSnapshot {
		var made = NativeKitAudio.nk_audio_mix_snapshot_create();
		AudioResult.check(made.status, "audio.mixSnapshot.create");
		return new MixSnapshot(made.out_snapshot);
	}

	public function nativeHandle():NativeKitAudio.MixSnapshotHandle {
		ensureLive();
		return value;
	}

	/** Captures the bus's configured volume and mute state. */
	public function captureBus(bus:Bus):Void {
		ensureLive();
		if (bus == null)
			throw "Audio mix snapshot bus must not be null";
		AudioResult.check(
			NativeKitAudio.nk_audio_mix_snapshot_capture_bus(value, bus.nativeHandle()),
			"audio.mixSnapshot.captureBus");
	}

	/** Sets or replaces a target for one bus. */
	public function setBus(bus:Bus, volume:Float, muted:Bool = false):Void {
		ensureLive();
		if (bus == null)
			throw "Audio mix snapshot bus must not be null";
		AudioResult.check(
			NativeKitAudio.nk_audio_mix_snapshot_set_bus(value, bus.nativeHandle(), volume, muted),
			"audio.mixSnapshot.setBus");
	}

	/** Removes one bus target. */
	public function removeBus(bus:Bus):Void {
		ensureLive();
		if (bus == null)
			throw "Audio mix snapshot bus must not be null";
		AudioResult.check(
			NativeKitAudio.nk_audio_mix_snapshot_remove_bus(value, bus.nativeHandle()),
			"audio.mixSnapshot.removeBus");
	}

	/** Removes every target from this snapshot. */
	public function clear():Void {
		ensureLive();
		AudioResult.check(NativeKitAudio.nk_audio_mix_snapshot_clear(value),
			"audio.mixSnapshot.clear");
	}

	/** Returns the number of live bus targets. */
	public function busCount():Int {
		ensureLive();
		var result = NativeKitAudio.nk_audio_mix_snapshot_get_bus_count(value);
		AudioResult.check(result.status, "audio.mixSnapshot.busCount");
		return result.out_count;
	}

	/** Applies all targets immediately or over a duration in PCM frames. */
	public function apply(durationFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_mix_snapshot_apply(value, durationFrames),
			"audio.mixSnapshot.apply");
	}

	/** Applies all targets at an absolute process-wide audio time. */
	public function applyAt(durationFrames:haxe.Int64, timeFrames:haxe.Int64):Void {
		ensureLive();
		AudioResult.check(
			NativeKitAudio.nk_audio_mix_snapshot_apply_at(value, durationFrames, timeFrames),
			"audio.mixSnapshot.applyAt");
	}

	/** Releases the snapshot. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.mixSnapshot.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Audio mix snapshot has been disposed";
	}
}
