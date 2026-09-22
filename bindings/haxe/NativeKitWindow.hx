import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
import NativeKitError;
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

	/** Replaces the logical hit-test regions used by custom native window chrome. */
	public function setDecorationRegions(regions:Array<WindowDecorationRegion>):Void {
		ensureLive();
		NativeKit.nk_window_set_decoration_regions_checked(value, regions);
	}

	public function createSurface(options:SurfaceOptions):NativeKitSurface {
		ensureLive();
		var surface = NativeKitSurface.adopt(NativeKit.nk_surface_create_checked(new Handle(value.rawValue()), options));
		surfaces.push(surface);
		return surface;
	}

	public function createWebView(options:WebviewOptions):NativeKitWebView {
		ensureLive();
		var webview = new NativeKitWebView(NativeKit.nk_webview_create_checked(new Handle(value.rawValue()), options));
		webviews.push(webview);
		return webview;
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
		if (status != null && status != Result.Ok)
			throw new NativeKitError(status, "window.dispose", NativeKit.nk_last_error());
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
