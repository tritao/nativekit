import NativeKit;

enum NativeKitEventValue {
	None;
	ClipboardText(request:haxe.Int64, result:Int, text:String);
	ClipboardFiles(request:haxe.Int64, result:Int, paths:Array<String>);
	DropText(source:Int, text:String);
	DropFiles(source:Int, paths:Array<String>);
	Raw(kind:Int, source:Int, request:haxe.Int64, result:Int, flags:Int, dataCount:Int, data:haxe.io.Bytes);
}

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
		kind = event.get_kind();
		source = event.get_source();
		request = event.get_request_id();
		result = event.get_result();
		flags = event.get_flags();
		dataCount = event.get_data_count();
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

	/** Decodes known text and packed-string event formats without releasing this event. */
	public function decode():NativeKitEventValue {
		var data = payload();
		return switch kind {
			case 0: None;
			case 400: ClipboardText(request, result, data.toString());
			case 401: ClipboardFiles(request, result, decodeClipboardFiles(data, dataCount));
			case 300: DropFiles(source, decodeDropItems(data, dataCount));
			case 301: DropText(source, decodeDropItems(data, dataCount).join(""));
			default: Raw(kind, source, request, result, flags, dataCount, data);
		}
	}

	/** Decodes and releases the event, including when decoding throws. */
	public function take():NativeKitEventValue {
		var value:NativeKitEventValue;
		try {
			value = decode();
		} catch (error:Dynamic) {
			release();
			throw error;
		}
		release();
		return value;
	}

	/** Releases the native payload. Returns false after the first release. */
	public function release():Bool {
		if (released)
			return false;
		NativeKit.nk_event_release(event);
		released = true;
		return true;
	}

	public function isReleased():Bool
		return released;

	/** Decodes an `nk_clipboard_files` payload and rejects inconsistent data. */
	public static function decodeClipboardFiles(data:haxe.io.Bytes, expectedCount:Int):Array<String>
		return decodePackedStrings(data, expectedCount, 8);

	/** Decodes an `nk_drop_data` payload and rejects inconsistent data. */
	public static function decodeDropItems(data:haxe.io.Bytes, expectedCount:Int):Array<String>
		return decodePackedStrings(data, expectedCount, 16);

	static function decodePackedStrings(data:haxe.io.Bytes, expectedCount:Int, headerSize:Int):Array<String> {
		if (data.length < headerSize)
			throw "NativeKit packed event payload is shorter than its header";
		var count = readU32(data, headerSize == 8 ? 0 : 8),
			offset = readU32(data, headerSize == 8 ? 4 : 12);
		if (count != expectedCount || offset < headerSize || offset > data.length)
			throw "NativeKit packed event payload has an invalid header";
		var values:Array<String> = [], cursor = offset;
		for (_ in 0...count) {
			var end = cursor;
			while (end < data.length && data.get(end) != 0)
				end++;
			if (end >= data.length)
				throw "NativeKit packed event payload has an unterminated string";
			values.push(data.getString(cursor, end - cursor));
			cursor = end + 1;
		}
		return values;
	}

	static function readU32(data:haxe.io.Bytes, offset:Int):Int {
		if (offset < 0 || offset > data.length - 4)
			throw "NativeKit packed event payload has a truncated integer";
		return data.get(offset) | data.get(offset + 1) << 8 | data.get(offset + 2) << 16 | data.get(offset + 3) << 24;
	}

	function ensureOpen():Void {
		if (released)
			throw "NativeKit event has already been released";
	}
}
