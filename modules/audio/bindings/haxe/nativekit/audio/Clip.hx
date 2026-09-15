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
	var disposed:Bool = false;

	private function new(owned:NativeKitAudio.OwnedClipHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public static function fromFile(path:String):Clip {
		if (path == null || path.length == 0)
			throw "Audio clip path must not be empty";
		var made = NativeKitAudio.nk_audio_clip_create_from_file(path);
		AudioResult.check(made.status, "audio.clip.fromFile");
		return new Clip(made.out_clip);
	}

	/** Creates a clip from a provider-backed URI resource. */
	public static function fromResource(resource:Resource):Clip {
		if (resource == null)
			throw "Audio clip resource must not be null";
		var made = NativeKitAudio.nk_audio_clip_create_from_resource(resource.nativeValue());
		AudioResult.check(made.status, "audio.clip.fromResource");
		return new Clip(made.out_clip);
	}

	public static function fromMemory(data:Bytes):Clip {
		if (data == null || data.length == 0)
			throw "Audio clip data must not be empty";
		var made = NativeKitAudio.nk_audio_clip_create_from_memory(data, data.length);
		AudioResult.check(made.status, "audio.clip.fromMemory");
		return new Clip(made.out_clip);
	}

	public function nativeHandle():NativeKitAudio.ClipHandle {
		ensureLive();
		return value;
	}

	public function createVoice(?options:VoiceOptions):Voice {
		ensureLive();
		return Voice.fromClip(this, options);
	}

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
