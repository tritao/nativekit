package nativekit.gpu;

import nativekit.gpu.Pipeline.PipelineBuilder;
import nativekit.gpu.Shader.ShaderBuilder;

/** Owns one renderer and all resources created through it. */
class Renderer {
	final surface:Surface;
	final resources:Array<Void->Void> = [];
	var value:nkgpu_renderer;
	var disposed:Bool = false;
	var frameActive:Bool = false;
	var activeRenderTarget:Null<RenderTarget> = null;

	private function new(surface:Surface, value:nkgpu_renderer) {
		this.surface = surface;
		this.value = value;
	}

	public static function create(surface:Surface):Renderer {
		var made = NativeKitGpu.nkgpu_renderer_create(surface.nativeHandle());
		GpuResult.check(made.status, "renderer.create");
		return new Renderer(surface, made.out_renderer);
	}

	public function nativeHandle():nkgpu_renderer {
		ensureLive();
		return value;
	}

	public function beginFrame():Void {
		ensureLive();
		if (frameActive)
			throw "GPU frame is already active";
		GpuResult.check(NativeKitGpu.nkgpu_begin_frame(value), "renderer.beginFrame");
		frameActive = true;
	}

	public function endFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "GPU frame is not active";
		if (activeRenderTarget != null)
			throw "End the active GPU render target before ending its pass";
		GpuResult.check(NativeKitGpu.nkgpu_end_frame(value), "renderer.endFrame");
		frameActive = false;
	}

	@:allow(RenderTarget)
	function beginRenderTarget(target:RenderTarget, clear:Bool):Void {
		ensureLive();
		if (frameActive)
			throw "A GPU pass is already active";
		if (target.rendererOwner() != this)
			throw "GPU render target belongs to another renderer";
		GpuResult.check(NativeKitGpu.nkgpu_begin_render_target(value, target.nativeHandle(), clear ? 1 : 0),
			"renderer.beginRenderTarget");
		frameActive = true;
		activeRenderTarget = target;
	}

	@:allow(RenderTarget)
	function endRenderTarget(target:RenderTarget):Void {
		ensureFrame();
		if (activeRenderTarget != target)
			throw "GPU render target is not active";
		GpuResult.check(NativeKitGpu.nkgpu_end_render_target(value), "renderer.endRenderTarget");
		frameActive = false;
		activeRenderTarget = null;
	}

	public function submit(commands:CommandBuffer):Void {
		ensureFrame();
		commands.ensureRenderer(this);
		GpuResult.check(NativeKitGpu.nkgpu_submit_commands(value, commands.data(), commands.size()), "renderer.submit");
	}

	public function commandBuffer(capacity:Int):CommandBuffer {
		ensureLive();
		return new CommandBuffer(this, capacity);
	}

	public function uniforms(size:Int):Uniforms {
		ensureFrame();
		if (size <= 0)
			throw "GPU uniform block size must be positive";
		var made = NativeKitGpu.nkgpu_uniforms_begin(value, size);
		GpuResult.check(made.status, "renderer.uniforms");
		return new Uniforms(this, made.out_builder);
	}

	public function draw(baseElement:Int, elementCount:Int, instanceCount:Int = 1):Void {
		ensureFrame();
		if (baseElement < 0 || elementCount <= 0 || instanceCount <= 0)
			throw "GPU draw range and instance count must be positive";
		GpuResult.check(NativeKitGpu.nkgpu_draw(value, baseElement, elementCount, instanceCount), "renderer.draw");
	}

	public function dispose():Void {
		if (disposed)
			return;
		if (frameActive)
			throw "Cannot dispose a GPU renderer during an active frame";
		GpuResult.check(NativeKitGpu.nkgpu_renderer_destroy(value), "renderer.dispose");
		disposed = true;
		for (release in resources)
			release();
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Buffer, Image, Sampler, Shader, Pipeline, RenderTarget, CommandBuffer, Uniforms)
	function ensureFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "GPU operation requires an active frame";
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, RenderTarget,
		CommandBuffer, Uniforms)
	function ensureLive():Void {
		if (disposed)
			throw "GPU renderer has been disposed";
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, RenderTarget,
		Uniforms)
	function registerResource(release:Void->Void):Void {
		ensureLive();
		resources.push(release);
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, RenderTarget,
		Uniforms)
	function ensureResourceOperation():Void {
		ensureLive();
		if (frameActive)
			throw "GPU resources can only change while the renderer is idle";
	}
}
