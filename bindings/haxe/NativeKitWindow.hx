import NativeKit;
import NativeKit.Handle;
import NativeKit.WindowHandle;
import NativeKit.OwnedWindowHandle;
import NativeKit.WindowOptions;
import NativeKit.SurfaceOptions;
import NativeKit.WebviewOptions;
import NativeKitResult;
import NativeKitSurface;
import NativeKitWebView;

/** Owns one NativeKit top-level window and resources created inside it. */
class NativeKitWindow {
	final value:WindowHandle;
	final owned:OwnedWindowHandle;
	final surfaces:Array<NativeKitSurface> = [];
	final webviews:Array<NativeKitWebView> = [];
	final dependentDisposers:Array<Void->Void> = [];
	var disposed:Bool = false;

	@:allow(NativeKitRuntime)
	private function new(owned:OwnedWindowHandle) {
		this.owned = owned;
		this.value = owned.borrow();
	}

	public function nativeHandle():WindowHandle {
		ensureLive();
		return value;
	}

	public function createSurface(options:SurfaceOptions):NativeKitSurface {
		ensureLive();
		var created = NativeKit.nk_surface_create(value, options);
		NativeKitResult.check(created.status, "surface.create");
		var surface = NativeKitSurface.adopt(created.out_surface);
		surfaces.push(surface);
		return surface;
	}

	public function createWebView(options:WebviewOptions):NativeKitWebView {
		ensureLive();
		var created = NativeKit.nk_webview_create(new Handle(value.rawValue()), options);
		NativeKitResult.check(created.status, "webview.create");
		var webview = new NativeKitWebView(created.out_webview);
		webviews.push(webview);
		return webview;
	}

	public function show(visible:Bool = true):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_window_show(value, visible), "window.show");
	}

	/** Registers an adapter-owned child that must be released before this window. */
	public function registerDependent(dispose:Void->Void):Void {
		ensureLive();
		dependentDisposers.push(dispose);
	}

	/** Destroys children before the window; safe to call more than once. */
	public function dispose():Void {
		if (disposed)
			return;
		var failure:Dynamic = null;
		for (index in 0...dependentDisposers.length) {
			try {
				dependentDisposers[dependentDisposers.length - 1 - index]();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		for (index in 0...webviews.length) {
			try {
				webviews[webviews.length - 1 - index].dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		for (index in 0...surfaces.length) {
			try {
				surfaces[surfaces.length - 1 - index].dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		if (failure != null)
			throw failure;
		disposed = true;
		var status = owned.close();
		if (status != null)
			NativeKitResult.check(status, "window.dispose");
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(NativeKitRuntime)
	function runtimeShutdown():Void {
		for (surface in surfaces)
			surface.runtimeShutdown();
		for (webview in webviews)
			webview.runtimeShutdown();
		disposed = true;
	}

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit window has been disposed";
	}
}
