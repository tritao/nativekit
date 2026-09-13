import NativeKit;
import NativeKit.WebViewHandle;
import NativeKitResult;

/** Owns one native child WebView. */
class NativeKitWebView {
	final value:WebViewHandle;
	var disposed:Bool = false;

	@:allow(NativeKitWindow)
	private function new(value:WebViewHandle)
		this.value = value;

	public function nativeHandle():WebViewHandle {
		ensureLive();
		return value;
	}

	public function show(visible:Bool = true):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_webview_show(value, visible), "webview.show");
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
