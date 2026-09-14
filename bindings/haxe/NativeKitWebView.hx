import NativeKit;
import NativeKit.WebViewHandle;
import NativeKit.OwnedWebViewHandle;

/** Owns one native child WebView. */
class NativeKitWebView {
	final value:WebViewHandle;
	final owned:OwnedWebViewHandle;
	var disposed:Bool = false;

	@:allow(NativeKitWindow)
	private function new(owned:OwnedWebViewHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public function nativeHandle():WebViewHandle {
		ensureLive();
		return value;
	}

	public function show(visible:Bool = true):Void {
		ensureLive();
		NativeKit.nk_webview_show_checked(value, visible);
	}

	public function navigate(url:String):Void {
		ensureLive();
		NativeKit.nk_webview_navigate_checked(value, url);
	}

	public function dispose():Void {
		if (disposed)
			return;
		disposed = true;
		var status = owned.close();
		if (status != null)
			NativeKitResult.check(status, "webview.dispose");
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
