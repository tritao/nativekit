import NativeKit.EventKind;
import NativeKitEventContext;
import NativeKitEventValue;

/** Decodes lifecycle and completion events emitted by the optional audio module. */
class NativeKitAudioEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue>
		return switch c.kind {
			case EventKind.AudioClipReady: AudioClipReady(c.source, c.request);
			case EventKind.AudioClipLoadFailed: AudioClipLoadFailed(c.source, c.request, c.result);
			case EventKind.AudioVoiceReady: AudioVoiceReady(c.source);
			case EventKind.AudioVoiceLoadFailed: AudioVoiceLoadFailed(c.source, c.result);
			case EventKind.AudioVoiceComplete: AudioVoiceComplete(c.source);
			case EventKind.AudioDeviceStarted: AudioDeviceStarted;
			case EventKind.AudioDeviceStopped: AudioDeviceStopped;
			case EventKind.AudioDeviceRerouted: AudioDeviceRerouted;
			case EventKind.AudioDeviceInterruptionBegan: AudioDeviceInterruptionBegan;
			case EventKind.AudioDeviceInterruptionEnded: AudioDeviceInterruptionEnded;
			default: null;
		}
}
