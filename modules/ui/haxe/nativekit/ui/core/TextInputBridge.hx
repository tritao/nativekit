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
			inputType:Int, flags:Int, cursor:Rect):Void {
		ensureLive();
		if (surface == null || surface.isDisposed() || !requestedActive || cursor == null ||
			(platformChecked && !platformSupported))
			return;
		var result = NativeKitTextInput.updateResult(surface, text == null ? "" : text, 0,
			documentLength, selectionStart,
			selectionEnd, compositionStart, compositionEnd, cast inputType, cast flags,
			null, cursor.x, cursor.y, cursor.width, cursor.height);
		checkPlatformResult(result, "text-input update");
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
}
