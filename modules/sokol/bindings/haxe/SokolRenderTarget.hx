import NativeKitSokol;
import NativeKit.GraphicsApi;

/** Typed offscreen Sokol target with a retained, backend-neutral image bridge. */
class SokolRenderTarget {
	private var renderer:nks_renderer;
	private var value:nks_render_target;
	private var disposed:Bool;
	public final width:Int;
	public final height:Int;

	private function new(renderer:nks_renderer, value:nks_render_target, width:Int, height:Int) {
		this.renderer = renderer;
		this.value = value;
		this.width = width;
		this.height = height;
		disposed = false;
	}

	public static function create(renderer:nks_renderer, width:Int, height:Int,
		depthStencil:Bool = false):SokolRenderTarget {
		if (width <= 0 || height <= 0)
			throw "Render-target dimensions must be positive";
		var made = NativeKitSokol.nks_render_target_create(renderer, width, height,
			depthStencil ? 1 : 0);
		SokolResult.check(made.status, "renderTarget.create");
		return new SokolRenderTarget(renderer, made.out_target, width, height);
	}

	/** Begins rendering to this target. Pair with end() before any other pass. */
	public function begin(clear:Bool = true):Void {
		ensureLive();
		SokolResult.check(NativeKitSokol.nks_begin_render_target(renderer, value, clear ? 1 : 0),
			"renderTarget.begin");
	}

	/** Ends the active offscreen pass. */
	public function end():Void {
		ensureLive();
		SokolResult.check(NativeKitSokol.nks_end_render_target(renderer), "renderTarget.end");
	}

	/**
	 * Returns an independently retained sampled image. It remains valid after
	 * this target is disposed, until the returned GraphicsImage is disposed.
	 */
	public function sampledImage():GraphicsImageRef {
		ensureLive();
		var borrowed = NativeKitSokol.nks_render_target_get_image(renderer, value);
		SokolResult.check(borrowed.status, "renderTarget.sampledImage");
		var api:GraphicsApi = NativeKitSokol.nks_query_graphics_api(renderer);
		if (api != GraphicsApi.Opengl && api != GraphicsApi.OpenglEs)
			throw "renderTarget.sampledImage encountered an unsupported backend";
		return GraphicsImageRef.fromBorrowedHandle(borrowed.out_image, width, height, api);
	}

	/** Releases the render target. Retained sampled images remain valid. */
	public function dispose():Void {
		if (disposed)
			return;
		SokolResult.check(NativeKitSokol.nks_render_target_destroy(renderer, value),
			"renderTarget.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol render target has been disposed";
	}
}
