package nativekit.ui.core;

import NativeKitSurface;
import NativeKit.Result;
import NativeKitTextInput;
import Rect;

/** Synchronizes Haxe editor state with NativeKit's custom-surface IME API. */
class TextInputBridge {
	var surface:Null<NativeKitSurface>;
	var requestedActive:Bool;
	var activeOwner:Null<WidgetId>;
	public var platformActive(default, null):Bool;
	public var platformSupported(default, null):Bool;
	public var platformChecked(default, null):Bool;
	var disposed:Bool;

	public function new() {
		surface = null;
		requestedActive = false;
		activeOwner = null;
		platformActive = false;
		platformSupported = false;
		platformChecked = false;
		disposed = false;
	}

	public function attach(surface:NativeKitSurface):Void {
		ensureLive();
		if (surface == null || surface.isDisposed())
			throw "Text input requires a live NativeKit surface";
		if (this.surface == surface)
			return;
		deactivatePlatform();
		this.surface = surface;
		platformSupported = false;
		platformChecked = false;
		if (requestedActive)
			activatePlatform();
	}

	/** Activates the platform editor for one focused UI text control. */
	public function activate(?owner:WidgetId):Void {
		ensureLive();
		if (owner != null && activeOwner != null && !activeOwner.equals(owner)) {
			requestedActive = false;
			deactivatePlatform();
		}
		if (owner != null)
			activeOwner = owner;
		requestedActive = true;
		activatePlatform();
	}

	/** Deactivates the platform editor, unless another control owns it now. */
	public function deactivate(?owner:WidgetId):Void {
		ensureLive();
		if (owner != null && (activeOwner == null || !activeOwner.equals(owner)))
			return;
		requestedActive = false;
		activeOwner = null;
		deactivatePlatform();
	}

	/** Returns whether this control owns the current active text-editor session. */
	public function isOwner(owner:WidgetId):Bool
		return owner != null && activeOwner != null && activeOwner.equals(owner);

	/** Publishes the active document, selection, composition and screen caret. */
	public function update(text:String, documentLength:Int, selectionStart:Int,
			selectionEnd:Int, compositionStart:Int, compositionEnd:Int,
			inputType:Int, flags:Int, cursor:Rect, selectionRects:Array<Rect>,
			compositionRects:Array<Rect>, ?selectionRangeRects:Array<TextRangeRect>,
			?compositionRangeRects:Array<TextRangeRect>):Void {
		ensureLive();
		if (surface == null || surface.isDisposed() || !requestedActive || cursor == null ||
			(platformChecked && !platformSupported))
			return;
		var result = NativeKitTextInput.updateResult(surface, text == null ? "" : text, 0,
			documentLength, selectionStart,
			selectionEnd, compositionStart, compositionEnd, cast inputType, cast flags,
			null, cursor.x, cursor.y, cursor.width, cursor.height);
		if (!checkPlatformResult(result, "text-input update"))
			return;
		result = NativeKitTextInput.updateGeometryResult(surface, selectionStart, selectionEnd,
			compositionStart, compositionEnd, encodeGeometry(selectionRects, selectionRangeRects),
			encodeGeometry(compositionRects, compositionRangeRects));
		checkPlatformResult(result, "text-input geometry update");
	}

	public function dispose():Void {
		if (disposed)
			return;
		requestedActive = false;
		activeOwner = null;
		deactivatePlatform();
		surface = null;
		disposed = true;
	}

	function activatePlatform():Void {
		if (platformActive || (platformChecked && !platformSupported) || surface == null ||
			surface.isDisposed())
			return;
		if (checkPlatformResult(NativeKitTextInput.setActiveResult(surface, true),
			"text-input activation"))
			platformActive = true;
	}

	function deactivatePlatform():Void {
		if (platformActive && platformSupported && surface != null && !surface.isDisposed())
			checkPlatformResult(NativeKitTextInput.setActiveResult(surface, false),
				"text-input deactivation");
		platformActive = false;
	}

	function checkPlatformResult(result:Result, operation:String):Bool {
		if (result == Result.ErrorUnsupported) {
			platformSupported = false;
			platformChecked = true;
			platformActive = false;
			return false;
		}
		if (result != Result.Ok)
			throw 'NativeKit $operation failed: $result';
		platformSupported = true;
		platformChecked = true;
		return true;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Text input bridge has been disposed";
	}

	static function encodeGeometry(rects:Null<Array<Rect>>,
			rangeRects:Null<Array<TextRangeRect>>):haxe.io.Bytes {
		var rectCount = rects == null ? 0 : rects.length;
		var rangeCount = rangeRects == null ? 0 : rangeRects.length;
		if (rectCount == 0 && rangeCount == 0)
			return haxe.io.Bytes.alloc(0);
		if (rectCount > Std.int(0x7fffffff / 20) || rangeCount > Std.int(0x7fffffff / 28) ||
			rectCount > Std.int((0x7fffffff - rangeCount * 28) / 20))
			throw "Text input geometry contains too many rectangles";
		var bytes = haxe.io.Bytes.alloc(rectCount * 20 + rangeCount * 28);
		var offset = 0;
		if (rects != null)
			for (rect in rects) {
				if (rect == null || !finite(rect.x) || !finite(rect.y) || !finite(rect.width) ||
					!finite(rect.height) || rect.width < 0.0 || rect.height < 0.0)
					throw "Text input geometry contains an invalid rectangle";
				bytes.setInt32(offset, 20);
				bytes.setFloat(offset + 4, rect.x);
				bytes.setFloat(offset + 8, rect.y);
				bytes.setFloat(offset + 12, rect.width);
				bytes.setFloat(offset + 16, rect.height);
				offset += 20;
			}
		if (rangeRects != null)
			for (rect in rangeRects) {
				if (rect == null || rect.start < 0 || rect.end < rect.start || !finite(rect.x) ||
					!finite(rect.y) || !finite(rect.width) || !finite(rect.height) ||
					rect.width < 0.0 || rect.height < 0.0)
					throw "Text input range geometry contains an invalid rectangle";
				bytes.setInt32(offset, 28);
				bytes.setFloat(offset + 4, rect.x);
				bytes.setFloat(offset + 8, rect.y);
				bytes.setFloat(offset + 12, rect.width);
				bytes.setFloat(offset + 16, rect.height);
				bytes.setInt32(offset + 20, rect.start);
				bytes.setInt32(offset + 24, rect.end);
				offset += 28;
			}
		return bytes;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
