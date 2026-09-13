package nativekit.gpu;

import NativeKitGpu;
import NativeKit.GraphicsApi;

/** Typed offscreen GPU target with a retained, backend-neutral image bridge. */
class RenderTarget {
	private var renderer:Renderer;
	private var value:nkgpu_render_target;
	private var disposed:Bool;
	public final width:Int;
	public final height:Int;

	private function new(renderer:Renderer, value:nkgpu_render_target, width:Int, height:Int) {
		this.renderer = renderer;
		this.value = value;
		this.width = width;
		this.height = height;
		disposed = false;
		renderer.registerResource(rendererClosed);
	}

	public static function create(renderer:Renderer, width:Int, height:Int,
		depthStencil:Bool = false):RenderTarget {
		if (width <= 0 || height <= 0)
			throw "Render-target dimensions must be positive";
		var made = NativeKitGpu.nkgpu_render_target_create(renderer.nativeHandle(), width, height,
			depthStencil ? 1 : 0);
		GpuResult.check(made.status, "renderTarget.create");
		return new RenderTarget(renderer, made.out_target, width, height);
	}

	public function nativeHandle():nkgpu_render_target {
		ensureLive();
		return value;
	}

	/** Begins rendering to this target. Pair with end() before any other pass. */
	public function begin(clear:Bool = true):Void {
		ensureLive();
		renderer.beginRenderTarget(this, clear);
	}

	/** Ends the active offscreen pass. */
	public function end():Void {
		if (disposed)
			throw "GPU render target has been disposed";
		renderer.endRenderTarget(this);
	}

	/**
	 * Returns an independently retained sampled image. It remains valid after
	 * this target is disposed, until the returned GraphicsImage is disposed.
	 */
	public function sampledImage():GraphicsImageRef {
		ensureLive();
		var borrowed = NativeKitGpu.nkgpu_render_target_get_image(renderer.nativeHandle(), value);
		GpuResult.check(borrowed.status, "renderTarget.sampledImage");
		var api:GraphicsApi = NativeKitGpu.nkgpu_query_graphics_api(renderer.nativeHandle());
		if (api != GraphicsApi.Opengl && api != GraphicsApi.OpenglEs)
			throw "renderTarget.sampledImage encountered an unsupported backend";
		return GraphicsImageRef.fromBorrowedHandle(borrowed.out_image, width, height, api);
	}

	/** Releases the render target. Retained sampled images remain valid. */
	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_render_target_destroy(renderer.nativeHandle(), value),
			"renderTarget.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function rendererOwner():Renderer
		return renderer;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "GPU render target has been disposed";
		renderer.ensureResourceOperation();
	}
}
