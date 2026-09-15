package nativekit.ui.core;

import NativeKit;
import NativeKit.Capabilities;
import NativeKit.CursorHandle;
import NativeKit.Handle;
import NativeKit.OwnedCursorHandle;
import NativeKit.WindowHandle;
import nativekit.ui.core.CursorShape as UiCursorShape;

/** Applies UI cursor intent to one NativeKit window with lazy native resources. */
class NativeCursorController {
	final window:WindowHandle;
	final cursors:Map<Int, OwnedCursorHandle>;
	var capabilityChecked:Bool;
	var available:Bool;
	var active:UiCursorShape;

	public function new(source:Handle) {
		if (source == null || !source.isValid())
			throw "Native cursor control requires a live window handle";
		window = new WindowHandle(source.rawValue());
		cursors = new Map();
		capabilityChecked = false;
		available = false;
		active = UiCursorShape.Arrow;
	}

	/** Best-effort application; cursor decoration must not break input delivery. */
	public function apply(shape:UiCursorShape):Void {
		if (shape == active)
			return;
		if (shape == UiCursorShape.Arrow) {
			if (available)
				setNativeCursor(CursorHandle.invalid());
			active = UiCursorShape.Arrow;
			return;
		}
		if (!supportsCursor())
			return;

		var key:Int = cast shape;
		var owner = cursors.get(key);
		if (owner == null || owner.isClosed()) {
			try {
				owner = NativeKit.nk_cursor_create_standard_checked(nativeShape(shape));
				cursors.set(key, owner);
			} catch (_:Dynamic) {
				available = false;
				return;
			}
		}
		if (setNativeCursor(owner.borrow()))
			active = shape;
	}

	/** Restores the platform cursor and releases standard cursor resources. */
	public function reset():Void {
		if (active != UiCursorShape.Arrow && available)
			setNativeCursor(CursorHandle.invalid());
		active = UiCursorShape.Arrow;
		var keys:Array<Int> = [];
		for (key in cursors.keys())
			keys.push(key);
		for (key in keys) {
			var owner = cursors.get(key);
			if (owner != null)
				owner.close();
			cursors.remove(key);
		}
	}

	function supportsCursor():Bool {
		if (capabilityChecked)
			return available;
		capabilityChecked = true;
		var capabilities = NativeKit.nk_get_capabilities();
		available = haxe.Int64.compare(
			haxe.Int64.and(capabilities, Capabilities.cursor()), haxe.Int64.ofInt(0)) != 0;
		return available;
	}

	function setNativeCursor(cursor:CursorHandle):Bool {
		try {
			NativeKit.nk_window_set_cursor_checked(window, cursor);
			return true;
		} catch (_:Dynamic) {
			available = false;
			return false;
		}
	}

	static function nativeShape(shape:UiCursorShape):NativeKit.CursorShape {
		return cast switch shape {
			case UiCursorShape.Text: NativeKit.CursorShape.Ibeam;
			case UiCursorShape.Crosshair: NativeKit.CursorShape.Crosshair;
			case UiCursorShape.Hand: NativeKit.CursorShape.Hand;
			case UiCursorShape.HorizontalResize: NativeKit.CursorShape.HorizontalResize;
			case UiCursorShape.VerticalResize: NativeKit.CursorShape.VerticalResize;
			case UiCursorShape.DiagonalResize: NativeKit.CursorShape.NwseResize;
			case UiCursorShape.Move: NativeKit.CursorShape.Move;
			case UiCursorShape.NotAllowed: NativeKit.CursorShape.NotAllowed;
			case _: NativeKit.CursorShape.Arrow;
		};
	}
}
