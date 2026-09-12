import NativeKit.EventKind;
import NativeKit.TextEditAction;

/** Synthetic payload tests for decoder validation and fallback behavior. */
class NativeKitEventDecoderTests {
	public static function run():Bool {
		var zeroInt = 0;
		// Request IDs are Int64; this also covers the core signed widening conversion.
		var zero:haxe.Int64 = zeroInt, empty = haxe.io.Bytes.alloc(0);
		var unknown = new NativeKitEventContext(0x7ffffffe, 7, zero, -2, 3, 4, empty);
		var rawOk = switch NativeKitEvent.decodeContext(unknown) {
			case Raw(kind, source, _, result, flags, count, data): kind == 0x7ffffffe && source == 7 && result == -2 && flags == 3 && count == 4 && data == empty;
			case _: false;
		};
		var none = new NativeKitEventContext(EventKind.None, 0, zero, 0, 0, 0, empty);
		var nonMatch = NativeKitWindowEvents.decode(none) == null && NativeKitInputEvents.decode(none) == null && NativeKitResourceEvents.decode(none) == null;

		var badDialog = haxe.io.Bytes.alloc(16);
		putU32(badDialog, 4, 1); putU32(badDialog, 8, 16); putU32(badDialog, 12, 16);
		var unterminated = haxe.io.Bytes.alloc(9);
		putU32(unterminated, 0, 1); putU32(unterminated, 4, 8); unterminated.set(8, 65);
		var badResource = haxe.io.Bytes.alloc(16);
		putU32(badResource, 4, 1); putU32(badResource, 8, 16); putU32(badResource, 12, 16);
		var shortKey = new NativeKitEventContext(EventKind.Key, 0, zero, 0, 0, 0, haxe.io.Bytes.alloc(15));
		var shortWindow = new NativeKitEventContext(EventKind.WindowResize, 0, zero, 0, 0, 0, haxe.io.Bytes.alloc(7));
		var shortEdit = new NativeKitEventContext(EventKind.TextEdit, 0, zero, 0, 0, 0, haxe.io.Bytes.alloc(47));
		var pathsPayload = haxe.io.Bytes.alloc(16);
		putU32(pathsPayload, 8, 16); putU32(pathsPayload, 12, 16);
		var pathsContext = new NativeKitEventContext(EventKind.DialogPathsComplete, 0, zero, 0, 1, 0, pathsPayload);
		var pathsOk = switch NativeKitEvent.decodeContext(pathsContext) {
			case DialogPaths(_, 0, false, paths): paths.length == 0;
			case _: false;
		};
		var messagePayload = haxe.io.Bytes.alloc(4);
		putU32(messagePayload, 0, 3);
		var messageContext = new NativeKitEventContext(EventKind.DialogMessageComplete, 0, zero, 0, 4, 0, messagePayload);
		var messageOk = switch NativeKitEvent.decodeContext(messageContext) {
			case DialogMessage(_, 0, 3): true;
			case _: false;
		};
		var resourcesPayload = haxe.io.Bytes.alloc(16);
		putU32(resourcesPayload, 8, 16); putU32(resourcesPayload, 12, 16);
		var resourcesContext = new NativeKitEventContext(EventKind.DialogResourcesComplete, 0, zero, 0, 5, 0, resourcesPayload);
		var resourcesOk = switch NativeKitEvent.decodeContext(resourcesContext) {
			case Resources(kind, _, result, accepted, items): kind == EventKind.DialogResourcesComplete && result == 0 && !accepted && items.length == 0;
			case _: false;
		};
		var editPayload = haxe.io.Bytes.alloc(50);
		putU32(editPayload, 0, TextEditAction.Compose);
		putU32(editPayload, 4, 48); putU32(editPayload, 8, 2);
		putU32(editPayload, 12, 1); putU32(editPayload, 16, 2);
		putU32(editPayload, 20, 2); putU32(editPayload, 24, 2);
		putU32(editPayload, 28, 1); putU32(editPayload, 32, 2);
		editPayload.set(48, 0xc3); editPayload.set(49, 0xa9);
		var editContext = new NativeKitEventContext(EventKind.TextEdit, 9, zero, 0, 0, 0, editPayload);
		var editOk = switch NativeKitEvent.decodeContext(editContext) {
			case TextEdit(9, edit): edit.action == TextEditAction.Compose && edit.text == "é" && edit.replaceStart == 1 && edit.compositionEnd == 2;
			case _: false;
		};
		var invalidUtf8 = haxe.io.Bytes.alloc(50);
		putU32(invalidUtf8, 4, 48); putU32(invalidUtf8, 8, 2);
		invalidUtf8.set(48, 0xc0); invalidUtf8.set(49, 0x80);

		if (!pathsOk) throw "path completion decoding failed";
		if (!messageOk) throw "message completion decoding failed";
		if (!resourcesOk) throw "resource completion decoding failed";
		return rawOk && nonMatch && editOk
			&& throws(function() { NativeKitEventBytes.requireSize(haxe.io.Bytes.alloc(3), 4); })
			&& throws(function() { NativeKitEventBytes.readU32(haxe.io.Bytes.alloc(3), 0); })
			&& throws(function() { NativeKitEventBytes.decodeDialogPaths(badDialog); })
			&& throws(function() { NativeKitEventBytes.decodeClipboardFiles(unterminated, 1); })
			&& throws(function() { NativeKitEventBytes.decodeResourceList(badResource, 0); })
			&& throws(function() { NativeKitInputEvents.decode(shortKey); })
			&& throws(function() { NativeKitInputEvents.decode(shortEdit); })
			&& throws(function() { NativeKitWindowEvents.decode(shortWindow); })
			&& throws(function() { NativeKitEventBytes.readUtf8Slice(invalidUtf8,48,2,48); });
	}

	static function putU32(data:haxe.io.Bytes, offset:Int, value:Int):Void {
		for (index in 0...4) data.set(offset + index, value >>> (index * 8) & 0xff);
	}

	static function throws(action:Void->Void):Bool {
		try action() catch (_:Dynamic) return true;
		return false;
	}
}
