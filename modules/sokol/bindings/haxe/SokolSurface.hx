import NativeKit.GraphicsApi;
import NativeKit.Handle;

/** Owns a Sokol-adapter surface associated with a NativeKit window. */
class SokolSurface {
	final window:NativeKitWindow;
	final renderers:Array<SokolRenderer> = [];
	var value:Handle;
	var disposed:Bool = false;

	private function new(window:NativeKitWindow, value:Handle) {
		this.window = window;
		this.value = value;
		window.registerDependent(function() {
			dispose();
		});
	}

	public static function create(window:NativeKitWindow, width:Int, height:Int):SokolSurface {
		if (width <= 0 || height <= 0)
			throw "Sokol surface dimensions must be positive";
		var made = NativeKitSokol.nks_surface_create(window.nativeHandle(), width, height);
		SokolResult.check(made.status, "surface.create");
		return new SokolSurface(window, made.out_surface);
	}

	public static function createForApi(window:NativeKitWindow, api:GraphicsApi, width:Int, height:Int):SokolSurface {
		if (width <= 0 || height <= 0)
			throw "Sokol surface dimensions must be positive";
		var made = NativeKitSokol.nks_surface_create_for_api(window.nativeHandle(), api, width, height);
		SokolResult.check(made.status, "surface.createForApi");
		return new SokolSurface(window, made.out_surface);
	}

	public function nativeHandle():Handle {
		ensureLive();
		return value;
	}

	public function createRenderer():SokolRenderer {
		ensureLive();
		var renderer = SokolRenderer.create(this);
		renderers.push(renderer);
		return renderer;
	}

	public function resize(width:Int, height:Int):Void {
		ensureLive();
		if (width <= 0 || height <= 0)
			throw "Sokol surface dimensions must be positive";
		SokolResult.check(NativeKitSokol.nks_surface_resize(value, width, height), "surface.resize");
	}

	public function dispose():Void {
		if (disposed)
			return;
		for (index in 0...renderers.length)
			renderers[renderers.length - 1 - index].dispose();
		SokolResult.check(NativeKitSokol.nks_surface_destroy(value), "surface.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolRenderer)
	function windowOwner():NativeKitWindow
		return window;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol surface has been disposed";
	}
}
