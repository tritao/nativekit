import NativeKit.EventKind;
import NativeKitEventContext;
import NativeKitEventValue;

/** Decodes lifecycle and completion events emitted by the optional audio module. */
class NativeKitAudioEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue>
		return switch c.kind {
			case EventKind.AudioVoiceReady: AudioVoiceReady(c.source);
			case EventKind.AudioVoiceLoadFailed: AudioVoiceLoadFailed(c.source, c.result);
			case EventKind.AudioVoiceComplete: AudioVoiceComplete(c.source);
			default: null;
		}
}
