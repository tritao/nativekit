import NativeKit;
import NativeKit.InitOptions;
import NativeKit.WindowOptions;
import NativeKitOptions;
import NativeKitResult;
import NativeKitWindow;

/** Owns one initialized NativeKit generation and all windows created through it. */
class NativeKitRuntime {
	final windows:Array<NativeKitWindow> = [];
	var disposed:Bool = false;

	private function new() {}

	public static function start(?options:InitOptions):NativeKitRuntime {
		var configured = options == null ? NativeKitOptions.init() : options;
		NativeKitResult.check(NativeKit.nk_init(configured), "runtime.start");
		return new NativeKitRuntime();
	}

	public function createWindow(options:WindowOptions):NativeKitWindow {
		ensureLive();
		var created = NativeKit.nk_window_create(options);
		NativeKitResult.check(created.status, "window.create");
		var window = new NativeKitWindow(created.out_window);
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
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "NativeKit runtime has been disposed";
	}
}
