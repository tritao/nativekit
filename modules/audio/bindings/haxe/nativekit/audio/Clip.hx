package nativekit.audio;

import NativeKit;
import NativeKitAudio;
import NativeKitError;
import haxe.io.Bytes;
import nativekit.resource.Resource;

/** Reusable audio source that can create multiple independent voices. */
class Clip {
	final value:NativeKitAudio.ClipHandle;
	final owned:NativeKitAudio.OwnedClipHandle;
	final request:haxe.Int64;
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedClipHandle, request:haxe.Int64) {
		this.owned = owned;
		this.value = owned.borrow();
		this.request = request;
	}

	public static function fromFile(path:String):Clip {
		if (path == null || path.length == 0)
			throw "Audio clip path must not be empty";
		var made = NativeKitAudio.nk_audio_clip_create_from_file(path);
		AudioResult.check(made.status, "audio.clip.fromFile");
		return new Clip(made.out_clip, haxe.Int64.ofInt(0));
	}

	/** Creates a clip from a provider-backed URI resource. */
	public static function fromResource(resource:Resource):Clip {
		if (resource == null)
			throw "Audio clip resource must not be null";
		var made = NativeKitAudio.nk_audio_clip_create_from_resource(resource.nativeValue());
		AudioResult.check(made.status, "audio.clip.fromResource");
		return new Clip(made.out_clip, haxe.Int64.ofInt(0));
	}

	/** Starts loading a reusable clip from a provider-backed URI resource. */
	public static function fromResourceAsync(resource:Resource):Clip {
		if (resource == null)
			throw "Audio clip resource must not be null";
		var made = NativeKitAudio.nk_audio_clip_create_from_resource_async(resource.nativeValue());
		AudioResult.check(made.status, "audio.clip.fromResourceAsync");
		return new Clip(made.out_clip, made.out_request);
	}

	public static function fromMemory(data:Bytes):Clip {
		if (data == null || data.length == 0)
			throw "Audio clip data must not be empty";
		var made = NativeKitAudio.nk_audio_clip_create_from_memory(data, data.length);
		AudioResult.check(made.status, "audio.clip.fromMemory");
		return new Clip(made.out_clip, haxe.Int64.ofInt(0));
	}

	public function nativeHandle():NativeKitAudio.ClipHandle {
		ensureLive();
		return value;
	}

	public function createVoice(?options:VoiceOptions):Voice {
		ensureLive();
		return Voice.fromClip(this, options);
	}

	/** Returns the asynchronous request ID, or zero for a synchronous clip. */
	public function loadRequest():haxe.Int64 {
		ensureLive();
		return request;
	}

	/** Returns the encoded resource loading state. */
	public function loadState():NativeKitAudio.ClipLoadState {
		ensureLive();
		var result = NativeKitAudio.nk_audio_clip_get_load_state(value);
		AudioResult.check(result.status, "audio.clip.loadState");
		return result.out_state;
	}

	/** Returns true once the clip can create playback voices. */
	public function isReady():Bool
		return loadState() == NativeKitAudio.ClipLoadState.Ready;

	/** Releases the clip; existing voices retain their source. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = owned.close();
		disposed = true;
		if (status != null && status != NativeKit.Result.Ok)
			throw new NativeKitError(status, "audio.clip.dispose", NativeKit.nk_last_error());
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Audio clip has been disposed";
	}
}
