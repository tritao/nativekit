import NativeKit;
import NativeKit.Handle;
import NativeKitResult;

/** Owns one native child WebView. */
class NativeKitWebView {
	final value:Handle;
	var disposed:Bool = false;

	@:allow(NativeKitWindow)
	private function new(value:Handle)
		this.value = value;

	public function nativeHandle():Handle {
		ensureLive();
		return value;
	}

	public function show(visible:Bool = true):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_webview_show(value, visible ? 1 : 0), "webview.show");
	}

	public function navigate(url:String):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_webview_navigate(value, url), "webview.navigate");
	}

	public function dispose():Void {
		if (disposed)
			return;
		NativeKitResult.check(NativeKit.nk_webview_destroy(value), "webview.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(NativeKitWindow)
	function runtimeShutdown():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit WebView has been disposed";
	}
}
