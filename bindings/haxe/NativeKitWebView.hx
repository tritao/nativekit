import nativekit.ffi.NativeKit;
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

	public static function create(parent:Handle, options:WebviewOptions):NativeKitWebView
		return new NativeKitWebView(NativeKit.nk_webview_create_checked(parent, options));

	public function show(visible:Bool):Void {
		ensureLive();
		check(NativeKit.nk_webview_show(value, visible), "webview.show");
	}

	public function setBounds(x:Int, y:Int, width:Int, height:Int):Void {
		ensureLive();
		check(NativeKit.nk_webview_set_bounds(value, x, y, width, height), "webview.setBounds");
	}

	public function setHtml(html:String, ?baseUrl:String):Void {
		ensureLive();
		check(NativeKit.nk_webview_set_html(value, html, baseUrl), "webview.setHtml");
	}

	public function navigate(url:String):Void {
		ensureLive();
		check(NativeKit.nk_webview_navigate(value, url), "webview.navigate");
	}

	public function reload():Void {
		ensureLive();
		check(NativeKit.nk_webview_reload(value), "webview.reload");
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

	static function check(status:Result, operation:String):Void {
		if (status != Result.Ok)
			throw new NativeKitError(status, operation, NativeKit.nk_last_error());
	}
}
