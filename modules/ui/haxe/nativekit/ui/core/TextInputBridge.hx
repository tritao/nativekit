package nativekit.ui.core;

import NativeKitSurface;
import NativeKitTextInput;
import Rect;

/** Synchronizes Haxe editor state with NativeKit's custom-surface IME API. */
class TextInputBridge {
	var surface:Null<NativeKitSurface>;
	var requestedActive:Bool;
	var platformActive:Bool;
	var disposed:Bool;

	public function new() {
		surface = null;
		requestedActive = false;
		platformActive = false;
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
		if (requestedActive)
			activatePlatform();
	}

	public function activate():Void {
		ensureLive();
		requestedActive = true;
		activatePlatform();
	}

	public function deactivate():Void {
		ensureLive();
		requestedActive = false;
		deactivatePlatform();
	}

	/** Publishes the active document, selection, composition and screen caret. */
	public function update(text:String, documentLength:Int, selectionStart:Int,
			selectionEnd:Int, compositionStart:Int, compositionEnd:Int,
			inputType:Int, flags:Int, cursor:Rect):Void {
		ensureLive();
		if (surface == null || surface.isDisposed() || !requestedActive || cursor == null)
			return;
		NativeKitTextInput.update(surface, text == null ? "" : text, 0, documentLength, selectionStart,
			selectionEnd, compositionStart, compositionEnd, cast inputType, cast flags,
			null, cursor.x, cursor.y, cursor.width, cursor.height);
	}

	public function dispose():Void {
		if (disposed)
			return;
		requestedActive = false;
		deactivatePlatform();
		surface = null;
		disposed = true;
	}

	function activatePlatform():Void {
		if (!platformActive && surface != null && !surface.isDisposed()) {
			NativeKitTextInput.setActive(surface, true);
			platformActive = true;
		}
	}

	function deactivatePlatform():Void {
		if (platformActive && surface != null && !surface.isDisposed())
			NativeKitTextInput.setActive(surface, false);
		platformActive = false;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Text input bridge has been disposed";
	}
}
