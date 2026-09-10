import NativeKit;
import NativeKit.NativeKitConstants;

enum NativeKitEventValue {
	None;
	ClipboardText(request:haxe.Int64, result:Int, text:String);
	ClipboardFiles(request:haxe.Int64, result:Int, paths:Array<String>);
	DropText(source:Int, text:String);
	DropFiles(source:Int, paths:Array<String>);
	DialogPaths(request:haxe.Int64, result:Int, accepted:Bool, paths:Array<String>);
	DialogMessage(request:haxe.Int64, result:Int, button:Int);
	WebViewNavigated(source:Int, url:String);
	WebViewMessage(source:Int, json:String);
	WebViewTitleChanged(source:Int, title:String);
	WebViewEvaluation(source:Int, request:haxe.Int64, result:Int, json:String);
	WebViewNavigationFailed(source:Int, category:Int, message:String);
	WebViewNavigationRequest(source:Int, request:haxe.Int64, url:String);
	NotificationActivated(request:haxe.Int64, action:String);
	NotificationFailed(request:haxe.Int64, message:String);
	Raw(kind:Int, source:Int, request:haxe.Int64, result:Int, flags:Int, dataCount:Int, data:haxe.io.Bytes);
	WindowClose(source:Int);
	WindowResize(source:Int, width:Int, height:Int);
	WindowMove(source:Int, x:Int, y:Int);
	WindowFramebufferResize(source:Int, width:Int, height:Int);
	WindowScaleChanged(source:Int, scale:Float);
	WindowStateChanged(source:Int, stateFlags:Int);
	Key(source:Int, key:Int, scancode:Int, action:Int, modifiers:Int);
	TextInput(source:Int, codepoint:Int);
	PointerMove(source:Int, x:Float, y:Float);
	PointerButton(source:Int, button:Int, action:Int, modifiers:Int, x:Float, y:Float);
	PointerScroll(source:Int, x:Float, y:Float);
	PointerEnter(source:Int, entered:Bool);
	Touch(source:Int, pointerId:Int, action:Int, tool:Int, modifiers:Int, x:Float, y:Float, pressure:Float, tiltX:Float, tiltY:Float);
	JoystickAxis(source:Int, axis:Int, value:Float);
	JoystickButton(source:Int, button:Int, pressed:Bool);
	JoystickHat(source:Int, hat:Int, value:Int);
	GamepadAxis(source:Int, axis:Int, value:Float);
	GamepadButton(source:Int, button:Int, pressed:Bool);
	SurfaceReady(source:Int);
	SurfaceResize(source:Int, width:Int, height:Int, framebufferWidth:Int, framebufferHeight:Int);
	SurfaceLost(source:Int);
	Resources(kind:Int, request:haxe.Int64, result:Int, accepted:Bool, items:Array<NativeKitResource>);
	ShareReceived(text:Null<String>, subject:Null<String>, items:Array<NativeKitResource>);
	ResourceDrop(source:Int, x:Float, y:Float, text:Null<String>, items:Array<NativeKitResource>);
}

class NativeKitResource {
	public final flags:Int;
	public final uri:String;
	public final mimeType:Null<String>;
	public final displayName:Null<String>;
	public function new(flags:Int, uri:String, mimeType:Null<String>, displayName:Null<String>) {
		this.flags = flags; this.uri = uri; this.mimeType = mimeType; this.displayName = displayName;
	}
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
			case NativeKitConstants.NK_EVENT_NONE: None;
			case NativeKitConstants.NK_EVENT_CLIPBOARD_TEXT_COMPLETE: ClipboardText(request, result, data.toString());
			case NativeKitConstants.NK_EVENT_CLIPBOARD_FILES_COMPLETE: ClipboardFiles(request, result, decodeClipboardFiles(data, dataCount));
			case NativeKitConstants.NK_EVENT_DROP_FILES: DropFiles(source, decodeDropItems(data, dataCount));
			case NativeKitConstants.NK_EVENT_DROP_TEXT: DropText(source, decodeDropItems(data, dataCount).join(""));
			case NativeKitConstants.NK_EVENT_DIALOG_COMPLETE:
				if (flags == NativeKitConstants.NK_BINDING_DIALOG_MESSAGE) {
					if (data.length != 4)
						throw "NativeKit message-dialog payload has an invalid size";
					DialogMessage(request, result, readU32(data, 0));
				} else {
					var paths = decodeDialogPaths(data);
					DialogPaths(request, result, readU32(data, 0) != 0, paths);
				}
			case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATED: WebViewNavigated(source, data.toString());
			case NativeKitConstants.NK_EVENT_WEBVIEW_MESSAGE: WebViewMessage(source, data.toString());
			case NativeKitConstants.NK_EVENT_WEBVIEW_TITLE_CHANGED: WebViewTitleChanged(source, data.toString());
			case NativeKitConstants.NK_EVENT_WEBVIEW_EVAL_COMPLETE: WebViewEvaluation(source, request, result, data.toString());
			case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATION_FAILED: WebViewNavigationFailed(source, flags, data.toString());
			case NativeKitConstants.NK_EVENT_WEBVIEW_NAVIGATION_REQUEST: WebViewNavigationRequest(source, request, data.toString());
			case NativeKitConstants.NK_EVENT_NOTIFICATION_ACTIVATED: NotificationActivated(request, data.toString());
			case NativeKitConstants.NK_EVENT_NOTIFICATION_FAILED: NotificationFailed(request, data.toString());
			case NativeKitConstants.NK_EVENT_WINDOW_CLOSE: WindowClose(source);
			case NativeKitConstants.NK_EVENT_WINDOW_RESIZE:
				requireSize(data, 8);
				var value:nk_window_resize_event = data;
				WindowResize(source, value.get_width(), value.get_height());
			case NativeKitConstants.NK_EVENT_WINDOW_MOVE:
				requireSize(data, 8);
				var value:nk_window_move_event = data;
				WindowMove(source, value.get_x(), value.get_y());
			case NativeKitConstants.NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE:
				requireSize(data, 8);
				var value:nk_window_framebuffer_resize_event = data;
				WindowFramebufferResize(source, value.get_width(), value.get_height());
			case NativeKitConstants.NK_EVENT_WINDOW_SCALE_CHANGED:
				requireSize(data, 4);
				var value:nk_window_scale_event = data;
				WindowScaleChanged(source, value.get_scale());
			case NativeKitConstants.NK_EVENT_WINDOW_STATE_CHANGED:
				requireSize(data, 24);
				var value:nk_window_state = data;
				WindowStateChanged(source, value.get_flags());
			case NativeKitConstants.NK_EVENT_KEY:
				requireSize(data, 16); var value:nk_key_event = data; Key(source, value.get_key(), value.get_scancode(), value.get_action(), value.get_modifiers());
			case NativeKitConstants.NK_EVENT_TEXT_INPUT:
				requireSize(data, 8); var value:nk_text_input_event = data; TextInput(source, value.get_codepoint());
			case NativeKitConstants.NK_EVENT_POINTER_MOVE:
				requireSize(data, 16); var value:nk_pointer_move_event = data; PointerMove(source, value.get_x(), value.get_y());
			case NativeKitConstants.NK_EVENT_POINTER_BUTTON:
				requireSize(data, 32); var value:nk_pointer_button_event = data; PointerButton(source, value.get_button(), value.get_action(), value.get_modifiers(), value.get_x(), value.get_y());
			case NativeKitConstants.NK_EVENT_POINTER_SCROLL:
				requireSize(data, 16); var value:nk_pointer_scroll_event = data; PointerScroll(source, value.get_x(), value.get_y());
			case NativeKitConstants.NK_EVENT_POINTER_ENTER: PointerEnter(source, flags != 0);
			case NativeKitConstants.NK_EVENT_TOUCH:
				requireSize(data, 48); var value:nk_touch_event = data; Touch(source, value.get_pointer_id(), value.get_action(), value.get_tool(), value.get_modifiers(), value.get_x(), value.get_y(), value.get_pressure(), value.get_tilt_x(), value.get_tilt_y());
			case NativeKitConstants.NK_EVENT_JOYSTICK_AXIS:
				requireSize(data, 8); var value:nk_joystick_axis_event = data; JoystickAxis(source, value.get_axis(), value.get_value());
			case NativeKitConstants.NK_EVENT_JOYSTICK_BUTTON:
				requireSize(data, 8); var value:nk_joystick_button_event = data; JoystickButton(source, value.get_button(), value.get_pressed() != 0);
			case NativeKitConstants.NK_EVENT_JOYSTICK_HAT:
				requireSize(data, 8); var value:nk_joystick_hat_event = data; JoystickHat(source, value.get_hat(), value.get_value());
			case NativeKitConstants.NK_EVENT_GAMEPAD_AXIS:
				requireSize(data, 8); var value:nk_gamepad_axis_event = data; GamepadAxis(source, value.get_axis(), value.get_value());
			case NativeKitConstants.NK_EVENT_GAMEPAD_BUTTON:
				requireSize(data, 8); var value:nk_gamepad_button_event = data; GamepadButton(source, value.get_button(), value.get_pressed() != 0);
			case NativeKitConstants.NK_EVENT_SURFACE_READY: SurfaceReady(source);
			case NativeKitConstants.NK_EVENT_SURFACE_RESIZE:
				requireSize(data, 16); var value:nk_surface_resize_event = data; SurfaceResize(source, value.get_width(), value.get_height(), value.get_framebuffer_width(), value.get_framebuffer_height());
			case NativeKitConstants.NK_EVENT_SURFACE_LOST: SurfaceLost(source);
			case NativeKitConstants.NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE | NativeKitConstants.NK_EVENT_RESOURCE_OPENED:
				var decoded = decodeResources(data, 0); Resources(kind, request, result, decoded.accepted, decoded.items);
			case NativeKitConstants.NK_EVENT_SHARE_RECEIVED:
				var listOffset = readU32(data, 0), decoded = decodeResources(data, listOffset);
				ShareReceived(readOptionalString(data, readU32(data, 4), 16), readOptionalString(data, readU32(data, 8), 16), decoded.items);
			case NativeKitConstants.NK_EVENT_RESOURCE_DROP:
				requireMinimumSize(data, 32); var header:nk_resource_drop = data; var decoded = decodeResources(data, header.get_resources_offset());
				ResourceDrop(source, header.get_x(), header.get_y(), readOptionalString(data, header.get_text_offset(), 32), decoded.items);
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

	/** Decodes an `nk_dialog_paths` payload and validates every path offset. */
	public static function decodeDialogPaths(data:haxe.io.Bytes):Array<String> {
		if (data.length < 16)
			throw "NativeKit dialog payload is shorter than its header";
		var count = readU32(data, 4), offsetsOffset = readU32(data, 8), stringsOffset = readU32(data, 12);
		if (offsetsOffset < 16 || stringsOffset < offsetsOffset || stringsOffset > data.length || count > Std.int((stringsOffset - offsetsOffset) / 4))
			throw "NativeKit dialog payload has an invalid offset table";
		var values:Array<String> = [];
		for (index in 0...count) {
			var offset = readU32(data, offsetsOffset + index * 4);
			if (offset < stringsOffset || offset >= data.length)
				throw "NativeKit dialog payload has an invalid path offset";
			var end = offset;
			while (end < data.length && data.get(end) != 0)
				end++;
			if (end >= data.length)
				throw "NativeKit dialog payload has an unterminated path";
			values.push(data.getString(offset, end - offset));
		}
		return values;
	}

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

	static function requireSize(data:haxe.io.Bytes, size:Int):Void {
		if (data.length != size)
			throw "NativeKit event payload has an invalid size";
	}

	static function requireMinimumSize(data:haxe.io.Bytes, size:Int):Void {
		if (data.length < size) throw "NativeKit event payload is truncated";
	}

	static function decodeResources(data:haxe.io.Bytes, listOffset:Int):{accepted:Bool, items:Array<NativeKitResource>} {
		requireMinimumSize(data, listOffset + 16);
		var accepted = readU32(data, listOffset) != 0, count = readU32(data, listOffset + 4), itemsOffset = readU32(data, listOffset + 8), stringsOffset = readU32(data, listOffset + 12);
		if (itemsOffset < listOffset + 16 || stringsOffset < itemsOffset || stringsOffset > data.length || count > Std.int((stringsOffset - itemsOffset) / 16)) throw "NativeKit resource payload has an invalid table";
		var items:Array<NativeKitResource> = [];
		for (index in 0...count) {
			var base = itemsOffset + index * 16, uri = readOptionalString(data, readU32(data, base + 4), stringsOffset);
			if (uri == null || uri.length == 0) throw "NativeKit resource payload has an invalid URI";
			items.push(new NativeKitResource(readU32(data, base), uri, readOptionalString(data, readU32(data, base + 8), stringsOffset), readOptionalString(data, readU32(data, base + 12), stringsOffset)));
		}
		return {accepted: accepted, items: items};
	}

	static function readOptionalString(data:haxe.io.Bytes, offset:Int, minimum:Int):Null<String> {
		if (offset == 0) return null;
		if (offset < minimum || offset >= data.length) throw "NativeKit resource payload has an invalid string offset";
		var end = offset; while (end < data.length && data.get(end) != 0) end++;
		if (end >= data.length) throw "NativeKit resource payload has an unterminated string";
		return data.getString(offset, end - offset);
	}

	function ensureOpen():Void {
		if (released)
			throw "NativeKit event has already been released";
	}
}
