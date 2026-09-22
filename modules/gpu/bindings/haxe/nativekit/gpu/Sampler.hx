package nativekit.gpu;
import nativekit.ffi.NativeKitGpu;

import nativekit.gpu.Enums.Filter;
import nativekit.gpu.Enums.Wrap;

/** Renderer-owned texture sampler with typed filter and wrap modes. */
class Sampler {
	final renderer:Renderer;
	final value:nkgpu_sampler;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_sampler) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function create(renderer:Renderer, minFilter:Filter = Filter.Nearest,
			magFilter:Filter = Filter.Nearest, wrapU:Wrap = Wrap.ClampToEdge,
			wrapV:Wrap = Wrap.ClampToEdge):Sampler {
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_sampler_create(renderer.nativeHandle(), minFilter, magFilter, wrapU, wrapV);
		GpuResult.check(made.status, "sampler.create");
		return new Sampler(renderer, made.out_sampler);
	}

	public function nativeHandle():nkgpu_sampler {
		ensureLive();
		return value;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "GPU sampler slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_sampler(renderer.nativeHandle(), slot, value), "sampler.apply");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_sampler_destroy(renderer.nativeHandle(), value), "sampler.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(CommandBuffer)
	function rendererOwner():Renderer
		return renderer;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "GPU sampler has been disposed";
		renderer.ensureLive();
	}
}
