import NativeKit.EventKind;
import NativeKitEventContext;
import NativeKitEventValue;

/** Decodes completion events emitted by the optional NativeKit audio module. */
class NativeKitAudioEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue>
		return switch c.kind {
			case EventKind.AudioVoiceComplete: AudioVoiceComplete(c.source);
			default: null;
		}
}
