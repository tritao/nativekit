import NativeKit;
import NativeKitEventContext;
import NativeKitEventValue;
import NativeKitInputEvents;
import NativeKitResourceEvents;
import NativeKitServiceEvents;
import NativeKitWindowEvents;

/** Owns one polled NativeKit event and releases its native payload exactly once. */
class NativeKitEvent {
	final event:nk_event;
	public final kind:Int;
	public final source:Int;
	public final request:haxe.Int64;
	public final result:Int;
	public final flags:Int;
	public final dataCount:Int;
	var released:Bool;

	function new(event:nk_event) {
		this.event = event;
		kind = event.get_kind(); source = event.get_source(); request = event.get_request_id();
		result = event.get_result(); flags = event.get_flags(); dataCount = event.get_data_count();
		released = false;
	}

	/** Polls one event. `None` is represented as an owned empty event. */
	public static function poll():NativeKitEvent {
		var event = new nk_event();
		event.set_struct_size(64);
		var polled = NativeKit.nk_poll_event(event);
		if (polled.status != 0) {
			NativeKit.nk_event_release(polled.event);
			throw 'NativeKit event poll failed: ${polled.status}';
		}
		return new NativeKitEvent(polled.event);
	}

	/** Copies the native payload. The returned bytes remain valid after release. */
	public function payload():haxe.io.Bytes {
		ensureOpen();
		return event.get_data_bytes();
	}

	/** Creates a managed decoder snapshot which remains valid after release. */
	public function snapshot():NativeKitEventContext
		return new NativeKitEventContext(kind, source, request, result, flags, dataCount, payload());

	/** Decodes a managed event snapshot without retaining native memory. */
	public static function decodeContext(context:NativeKitEventContext):NativeKitEventValue {
		var value = NativeKitWindowEvents.decode(context);
		if (value == null) value = NativeKitInputEvents.decode(context);
		if (value == null) value = NativeKitServiceEvents.decode(context);
		if (value == null) value = NativeKitResourceEvents.decode(context);
		return value != null ? value : Raw(context.kind, context.source, context.request,
			context.result, context.flags, context.dataCount, context.data);
	}

	/** Decodes known event formats without releasing this event. */
	public function decode():NativeKitEventValue
		return decodeContext(snapshot());

	/** Decodes and releases the event, including when decoding throws. */
	public function take():NativeKitEventValue {
		var value:NativeKitEventValue;
		try value = decode() catch (error:Dynamic) {
			release();
			throw error;
		}
		release();
		return value;
	}

	/** Releases the native payload. Returns false after the first release. */
	public function release():Bool {
		if (released) return false;
		NativeKit.nk_event_release(event);
		released = true;
		return true;
	}

	public function isReleased():Bool return released;

	function ensureOpen():Void
		if (released) throw "NativeKit event has already been released";
}
