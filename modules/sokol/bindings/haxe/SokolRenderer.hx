/** Owns one renderer and all resources created through it. */
class SokolRenderer {
	final surface:SokolSurface;
	final resources:Array<Void->Void> = [];
	var value:nks_renderer;
	var disposed:Bool = false;
	var frameActive:Bool = false;

	private function new(surface:SokolSurface, value:nks_renderer) {
		this.surface = surface;
		this.value = value;
	}

	public static function create(surface:SokolSurface):SokolRenderer {
		var made = NativeKitSokol.nks_renderer_create(surface.nativeHandle());
		SokolResult.check(made.status, "renderer.create");
		return new SokolRenderer(surface, made.out_renderer);
	}

	public function nativeHandle():nks_renderer {
		ensureLive();
		return value;
	}

	public function beginFrame():Void {
		ensureLive();
		if (frameActive)
			throw "Sokol frame is already active";
		SokolResult.check(NativeKitSokol.nks_begin_frame(value), "renderer.beginFrame");
		frameActive = true;
	}

	public function endFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "Sokol frame is not active";
		SokolResult.check(NativeKitSokol.nks_end_frame(value), "renderer.endFrame");
		frameActive = false;
	}

	public function submit(commands:SokolCommandBuffer):Void {
		ensureFrame();
		commands.ensureRenderer(this);
		SokolResult.check(NativeKitSokol.nks_submit_commands(value, commands.data(), commands.size()), "renderer.submit");
	}

	public function commandBuffer(capacity:Int):SokolCommandBuffer {
		ensureLive();
		return new SokolCommandBuffer(this, capacity);
	}

	public function uniforms(size:Int):SokolUniforms {
		ensureFrame();
		if (size <= 0)
			throw "Sokol uniform block size must be positive";
		var made = NativeKitSokol.nks_uniforms_begin(value, size);
		SokolResult.check(made.status, "renderer.uniforms");
		return new SokolUniforms(this, made.out_builder);
	}

	public function draw(baseElement:Int, elementCount:Int, instanceCount:Int = 1):Void {
		ensureFrame();
		if (baseElement < 0 || elementCount <= 0 || instanceCount <= 0)
			throw "Sokol draw range and instance count must be positive";
		SokolResult.check(NativeKitSokol.nks_draw(value, baseElement, elementCount, instanceCount), "renderer.draw");
	}

	public function dispose():Void {
		if (disposed)
			return;
		if (frameActive)
			throw "Cannot dispose a Sokol renderer during an active frame";
		SokolResult.check(NativeKitSokol.nks_renderer_destroy(value), "renderer.dispose");
		disposed = true;
		for (release in resources)
			release();
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolBuffer, SokolImage, SokolSampler, SokolShader, SokolPipeline, SokolRenderTarget, SokolCommandBuffer, SokolUniforms)
	function ensureFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "Sokol operation requires an active frame";
	}

	function ensureLive():Void {
		if (disposed)
			throw "Sokol renderer has been disposed";
	}

	@:allow(SokolBuffer, SokolImage, SokolSampler, SokolShader, SokolShaderBuilder, SokolPipeline, SokolPipelineBuilder, SokolRenderTarget,
		SokolUniforms)
	function registerResource(release:Void->Void):Void {
		ensureLive();
		resources.push(release);
	}

	@:allow(SokolBuffer, SokolImage, SokolSampler, SokolShader, SokolShaderBuilder, SokolPipeline, SokolPipelineBuilder, SokolRenderTarget,
		SokolUniforms)
	function ensureResourceOperation():Void
		ensureLive();
}
