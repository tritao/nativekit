import NativeKit;
import NativeKit.WebViewHandle;
import NativeKit.OwnedWebViewHandle;
import NativeKit.Result;
import NativeKitError;

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

	public function dispose():Void {
		if (disposed)
			return;
		disposed = true;
		var status = owned.close();
		if (status != null && status != Result.Ok)
			throw new NativeKitError(status, "webview.dispose", NativeKit.nk_last_error());
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
