package nativekit.gpu;

import nativekit.gpu.GpuResult;
import nativekit.ffi.NativeKitGpu;

/** One consumed uniform block staged for the current renderer frame. */
class Uniforms {
	final renderer:Renderer;
	final value:nkgpu_uniform_builder;
	var consumed:Bool = false;

	@:allow(Renderer)
	private function new(renderer:Renderer, value:nkgpu_uniform_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function writeFloat(offset:Int, number:Float):Uniforms {
		ensureLive();
		if (offset < 0)
			throw "GPU uniform offset must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_uniforms_write_f32(value, offset, number), "uniforms.writeFloat");
		return this;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		if (slot < 0)
			throw "GPU uniform slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_uniforms(renderer.nativeHandle(), slot, value), "uniforms.apply");
		consumed = true;
	}

	@:allow(Renderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "GPU uniform builder has been consumed";
		renderer.ensureFrame();
	}
}
