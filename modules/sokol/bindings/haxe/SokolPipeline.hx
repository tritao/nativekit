import SokolEnums.SokolIndexType;
import SokolEnums.SokolVertexFormat;

/** Renderer-owned pipeline resource. */
class SokolPipeline {
	final renderer:SokolRenderer;
	final value:nks_pipeline;
	var disposed:Bool = false;

	@:allow(SokolPipelineBuilder)
	private function new(renderer:SokolRenderer, value:nks_pipeline) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function begin(renderer:SokolRenderer, shader:SokolShader, stride:Int):SokolPipelineBuilder {
		if (shader.rendererOwner() != renderer)
			throw "Sokol pipeline shader belongs to a different renderer";
		if (stride <= 0)
			throw "Sokol pipeline vertex stride must be positive";
		var made = NativeKitSokol.nks_pipeline_begin(renderer.nativeHandle(), shader.nativeHandle(), stride);
		SokolResult.check(made.status, "pipeline.begin");
		return new SokolPipelineBuilder(renderer, made.out_builder);
	}

	public function nativeHandle():nks_pipeline {
		ensureLive();
		return value;
	}

	public function apply():Void {
		ensureLive();
		renderer.ensureFrame();
		SokolResult.check(NativeKitSokol.nks_apply_pipeline(renderer.nativeHandle(), value), "pipeline.apply");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		SokolResult.check(NativeKitSokol.nks_pipeline_destroy(renderer.nativeHandle(), value), "pipeline.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolCommandBuffer)
	function rendererOwner():SokolRenderer
		return renderer;

	@:allow(SokolRenderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol pipeline has been disposed";
		renderer.ensureResourceOperation();
	}
}

/** Configures one vertex/index layout before creating an immutable pipeline. */
class SokolPipelineBuilder {
	final renderer:SokolRenderer;
	final value:nks_pipeline_builder;
	var consumed:Bool = false;

	@:allow(SokolPipeline)
	private function new(renderer:SokolRenderer, value:nks_pipeline_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function attribute(location:Int, bufferIndex:Int, offset:Int, format:SokolVertexFormat):SokolPipelineBuilder {
		ensureLive();
		if (location < 0 || bufferIndex < 0 || offset < 0)
			throw "Sokol vertex attribute indices and offset must be non-negative";
		SokolResult.check(NativeKitSokol.nks_pipeline_attribute(value, location, bufferIndex, offset, format), "pipeline.attribute");
		return this;
	}

	public function depthStencil(enabled:Bool):SokolPipelineBuilder {
		ensureLive();
		SokolResult.check(NativeKitSokol.nks_pipeline_depth_stencil(value, enabled ? 1 : 0), "pipeline.depthStencil");
		return this;
	}

	public function indexType(type:SokolIndexType):SokolPipelineBuilder {
		ensureLive();
		SokolResult.check(NativeKitSokol.nks_pipeline_index_type(value, type), "pipeline.indexType");
		return this;
	}

	public function build():SokolPipeline {
		ensureLive();
		var made = NativeKitSokol.nks_pipeline_end(value);
		consumed = true;
		SokolResult.check(made.status, "pipeline.end");
		return new SokolPipeline(renderer, made.out_pipeline);
	}

	@:allow(SokolRenderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "Sokol pipeline builder has been consumed";
		renderer.ensureResourceOperation();
	}
}
