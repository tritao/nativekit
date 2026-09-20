package nativekit.gpu;

import NativeKit.GraphicsApi;

/** Owns a GPU-adapter surface associated with a NativeKit window. */
class Surface {
	final window:NativeKitWindow;
	final renderers:Array<Renderer> = [];
	var value:NativeKit.SurfaceHandle;
	var disposed:Bool = false;

	private function new(window:NativeKitWindow, value:NativeKit.SurfaceHandle) {
		this.window = window;
		this.value = value;
		window.registerDependent(function() {
			dispose();
		});
	}

	public static function create(window:NativeKitWindow, width:Int, height:Int):Surface {
		if (width <= 0 || height <= 0)
			throw "GPU surface dimensions must be positive";
		var made = NativeKitGpu.nkgpu_surface_create(window.nativeHandle(), width, height);
		GpuResult.check(made.status, "surface.create");
		return new Surface(window, made.out_surface);
	}

	public static function createForApi(window:NativeKitWindow, api:GraphicsApi, width:Int, height:Int):Surface {
		if (width <= 0 || height <= 0)
			throw "GPU surface dimensions must be positive";
		var made = NativeKitGpu.nkgpu_surface_create_for_api(window.nativeHandle(), api, width, height);
		GpuResult.check(made.status, "surface.createForApi");
		return new Surface(window, made.out_surface);
	}

	public function nativeHandle():NativeKit.SurfaceHandle {
		ensureLive();
		return value;
	}

	/** Acquires an immutable surface frame for explicit batch submission. */
	public function acquireFrame():SurfaceFrame {
		ensureLive();
		var target = new NativeKit.SurfaceFrameTarget();
		target.set_struct_size(80);
		var acquired = NativeKit.nk_surface_acquire_frame(value, target);
		if (acquired.status != NativeKit.Result.Ok)
			throw new NativeKitError(acquired.status, "surface.acquireFrame", NativeKit.nk_last_error());
		return new SurfaceFrame(this, acquired.out_frame, acquired.out_target);
	}

	public function createRenderer():Renderer {
		ensureLive();
		var renderer = Renderer.create(this);
		renderers.push(renderer);
		return renderer;
	}

	public function resize(width:Int, height:Int):Void {
		ensureLive();
		if (width <= 0 || height <= 0)
			throw "GPU surface dimensions must be positive";
		GpuResult.check(NativeKitGpu.nkgpu_surface_resize(value, width, height), "surface.resize");
	}

	public function dispose():Void {
		if (disposed)
			return;
		for (index in 0...renderers.length)
			renderers[renderers.length - 1 - index].dispose();
		GpuResult.check(NativeKitGpu.nkgpu_surface_destroy(value), "surface.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function windowOwner():NativeKitWindow
		return window;

	function ensureLive():Void {
		if (disposed)
			throw "GPU surface has been disposed";
	}
}
