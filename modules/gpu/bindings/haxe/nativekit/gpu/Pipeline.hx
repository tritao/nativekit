package nativekit.gpu;
import nativekit.ffi.NativeKitGpu;

import nativekit.gpu.Enums.IndexType;
import nativekit.gpu.Enums.VertexFormat;

/** Renderer-owned pipeline resource. */
class Pipeline {
	final renderer:Renderer;
	final value:nkgpu_pipeline;
	var disposed:Bool = false;

	@:allow(PipelineBuilder)
	private function new(renderer:Renderer, value:nkgpu_pipeline) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function begin(renderer:Renderer, shader:Shader, stride:Int):PipelineBuilder {
		renderer.ensureResourceOperation();
		if (shader.rendererOwner() != renderer)
			throw "GPU pipeline shader belongs to a different renderer";
		if (stride <= 0)
			throw "GPU pipeline vertex stride must be positive";
		var made = NativeKitGpu.nkgpu_pipeline_begin(renderer.nativeHandle(), shader.nativeHandle(), stride);
		GpuResult.check(made.status, "pipeline.begin");
		return new PipelineBuilder(renderer, made.out_builder);
	}

	/** Begins a compute-only pipeline builder. */
	public static function beginCompute(renderer:Renderer, shader:Shader):PipelineBuilder {
		renderer.ensureResourceOperation();
		if (shader.rendererOwner() != renderer)
			throw "GPU compute pipeline shader belongs to a different renderer";
		var made = NativeKitGpu.nkgpu_pipeline_begin_compute(renderer.nativeHandle(), shader.nativeHandle());
		GpuResult.check(made.status, "pipeline.beginCompute");
		return new PipelineBuilder(renderer, made.out_builder);
	}

	public function nativeHandle():nkgpu_pipeline {
		ensureLive();
		return value;
	}

	public function apply():Void {
		ensureLive();
		renderer.ensureFrame();
		GpuResult.check(NativeKitGpu.nkgpu_apply_pipeline(renderer.nativeHandle(), value), "pipeline.apply");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_pipeline_destroy(renderer.nativeHandle(), value), "pipeline.dispose");
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
			throw "GPU pipeline has been disposed";
		renderer.ensureLive();
	}
}

/** Configures one vertex/index layout before creating an immutable pipeline. */
class PipelineBuilder {
	final renderer:Renderer;
	final value:nkgpu_pipeline_builder;
	var consumed:Bool = false;

	@:allow(Pipeline)
	private function new(renderer:Renderer, value:nkgpu_pipeline_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function attribute(location:Int, bufferIndex:Int, offset:Int, format:VertexFormat):PipelineBuilder {
		ensureLive();
		if (location < 0 || bufferIndex < 0 || offset < 0)
			throw "GPU vertex attribute indices and offset must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_pipeline_attribute(value, location, bufferIndex, offset, format), "pipeline.attribute");
		return this;
	}

	public function depthStencil(enabled:Bool):PipelineBuilder {
		ensureLive();
		GpuResult.check(NativeKitGpu.nkgpu_pipeline_depth_stencil(value, enabled ? 1 : 0), "pipeline.depthStencil");
		return this;
	}

	/** Marks this pipeline builder as compute-only. */
	public function compute():PipelineBuilder {
		ensureLive();
		GpuResult.check(NativeKitGpu.nkgpu_pipeline_compute(value), "pipeline.compute");
		return this;
	}

	public function indexType(type:IndexType):PipelineBuilder {
		ensureLive();
		GpuResult.check(NativeKitGpu.nkgpu_pipeline_index_type(value, type), "pipeline.indexType");
		return this;
	}

	public function build():Pipeline {
		ensureLive();
		var made = NativeKitGpu.nkgpu_pipeline_end(value);
		consumed = true;
		GpuResult.check(made.status, "pipeline.end");
		return new Pipeline(renderer, made.out_pipeline);
	}

	@:allow(Renderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "GPU pipeline builder has been consumed";
		renderer.ensureResourceOperation();
	}
}
