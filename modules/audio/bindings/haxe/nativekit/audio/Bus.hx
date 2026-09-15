package nativekit.audio;

import NativeKitAudio;
import NativeKit;
import NativeKitError;

/** A mixer bus that groups sounds under shared volume and transport controls. */
class Bus {
	final value:NativeKitAudio.BusHandle;
	final owned:NativeKitAudio.OwnedBusHandle;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedBusHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function create():Bus {
		var made = NativeKitAudio.nk_audio_bus_create();
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

	/** Releases the bus; sounds already routed through it retain native ownership. */
	public function dispose():Void {
		if (disposed)
			return;
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
