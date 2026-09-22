import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
import NativeKitWindow;
import NativeKitEvents;

/** Owns one initialized NativeKit generation and all windows created through it. */
class NativeKitRuntime {
	final windows:Array<NativeKitWindow> = [];
	public final events:NativeKitEvents;
	var disposed:Bool = false;

	private function new() {
		events = new NativeKitEvents();
	}

	public static function start(?options:InitOptions):NativeKitRuntime {
		var configured = options;
		if (configured == null) {
			configured = new InitOptions();
			configured.set_api_version(NativeKit.nk_api_version());
		}
		NativeKit.nk_init_checked(configured);
		return new NativeKitRuntime();
	}

	public function createWindow(options:WindowOptions):NativeKitWindow {
		ensureLive();
		var owned = NativeKit.nk_window_create_checked(options);
		var window = new NativeKitWindow(owned);
		windows.push(window);
		return window;
	}

	/** Destroys owned children, then ends the NativeKit generation. */
	public function dispose():Void {
		if (disposed)
			return;
		var failure:Dynamic = null;
		for (index in 0...windows.length) {
			var window = windows[windows.length - 1 - index];
			try {
				window.dispose();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		// Keep the generation alive when a dependent refuses to close; callers
		// can release that resource and retry without invalidating live wrappers.
		if (failure != null)
			throw failure;
		NativeKit.nk_shutdown();
		disposed = true;
		for (window in windows)
			window.runtimeShutdown();
		events.runtimeShutdown();
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit runtime has been disposed";
	}
}
