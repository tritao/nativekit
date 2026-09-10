import NativeKit.NativeKitConstants;

/** Synthetic payload tests for decoder validation and fallback behavior. */
class NativeKitEventDecoderTests {
	public static function run():Bool {
		var nativeEvent = new nk_event();
		var zero = nativeEvent.get_request_id(), empty = haxe.io.Bytes.alloc(0);
		var unknown = new NativeKitEventContext(0x7ffffffe, 7, zero, -2, 3, 4, empty);
		var rawOk = switch NativeKitEvent.decodeContext(unknown) {
			case Raw(kind, source, _, result, flags, count, data): kind == 0x7ffffffe && source == 7 && result == -2 && flags == 3 && count == 4 && data == empty;
			case _: false;
		};
		var none = new NativeKitEventContext(NativeKitConstants.NK_EVENT_NONE, 0, zero, 0, 0, 0, empty);
		var nonMatch = NativeKitWindowEvents.decode(none) == null && NativeKitInputEvents.decode(none) == null && NativeKitResourceEvents.decode(none) == null;

		var badDialog = haxe.io.Bytes.alloc(16);
		putU32(badDialog, 4, 1); putU32(badDialog, 8, 16); putU32(badDialog, 12, 16);
		var unterminated = haxe.io.Bytes.alloc(9);
		putU32(unterminated, 0, 1); putU32(unterminated, 4, 8); unterminated.set(8, 65);
		var badResource = haxe.io.Bytes.alloc(16);
		putU32(badResource, 4, 1); putU32(badResource, 8, 16); putU32(badResource, 12, 16);
		var shortKey = new NativeKitEventContext(NativeKitConstants.NK_EVENT_KEY, 0, zero, 0, 0, 0, haxe.io.Bytes.alloc(15));

		return rawOk && nonMatch
			&& throws(function() { NativeKitEventBytes.requireSize(haxe.io.Bytes.alloc(3), 4); })
			&& throws(function() { NativeKitEventBytes.readU32(haxe.io.Bytes.alloc(3), 0); })
			&& throws(function() { NativeKitEventBytes.decodeDialogPaths(badDialog); })
			&& throws(function() { NativeKitEventBytes.decodeClipboardFiles(unterminated, 1); })
			&& throws(function() { NativeKitEventBytes.decodeResourceList(badResource, 0); })
			&& throws(function() { NativeKitInputEvents.decode(shortKey); });
	}

	static function putU32(data:haxe.io.Bytes, offset:Int, value:Int):Void {
		for (index in 0...4) data.set(offset + index, value >>> (index * 8) & 0xff);
	}

	static function throws(action:Void->Void):Bool {
		try action() catch (_:Dynamic) return true;
		return false;
	}
}
