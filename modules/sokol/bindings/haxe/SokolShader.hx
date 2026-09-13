import SokolEnums.SokolShaderStage;
import SokolEnums.SokolUniformType;

/** Renderer-owned shader resource. */
class SokolShader {
	final renderer:SokolRenderer;
	final value:nks_shader;
	var disposed:Bool = false;

	@:allow(SokolShaderBuilder)
	private function new(renderer:SokolRenderer, value:nks_shader) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function create(renderer:SokolRenderer, vertexSource:String, fragmentSource:String):SokolShader {
		var made = NativeKitSokol.nks_shader_create(renderer.nativeHandle(), vertexSource, fragmentSource);
		SokolResult.check(made.status, "shader.create");
		return new SokolShader(renderer, made.out_shader);
	}

	public static function begin(renderer:SokolRenderer, vertexSource:String, fragmentSource:String):SokolShaderBuilder {
		var made = NativeKitSokol.nks_shader_begin(renderer.nativeHandle(), vertexSource, fragmentSource);
		SokolResult.check(made.status, "shader.begin");
		return new SokolShaderBuilder(renderer, made.out_builder);
	}

	public function nativeHandle():nks_shader {
		ensureLive();
		return value;
	}

	@:allow(SokolPipeline)
	function rendererOwner():SokolRenderer
		return renderer;

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		SokolResult.check(NativeKitSokol.nks_shader_destroy(renderer.nativeHandle(), value), "shader.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolRenderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol shader has been disposed";
		renderer.ensureResourceOperation();
	}
}

/** Configures shader bindings before creating the immutable shader resource. */
class SokolShaderBuilder {
	final renderer:SokolRenderer;
	final value:nks_shader_builder;
	var consumed:Bool = false;

	@:allow(SokolShader)
	private function new(renderer:SokolRenderer, value:nks_shader_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function uniformBlock(slot:Int, stage:SokolShaderStage, size:Int):SokolShaderBuilder {
		ensureLive();
		if (slot < 0 || size <= 0)
			throw "Sokol uniform block slot and size are invalid";
		SokolResult.check(NativeKitSokol.nks_shader_uniform_block(value, slot, stage, size), "shader.uniformBlock");
		return this;
	}

	public function uniform(slot:Int, memberIndex:Int, name:String, type:SokolUniformType, arrayCount:Int = 1):SokolShaderBuilder {
		ensureLive();
		if (slot < 0 || memberIndex < 0 || arrayCount < 0)
			throw "Sokol uniform description is invalid";
		SokolResult.check(NativeKitSokol.nks_shader_uniform(value, slot, memberIndex, name, type, arrayCount), "shader.uniform");
		return this;
	}

	public function texture(viewSlot:Int, samplerSlot:Int, stage:SokolShaderStage, name:String):SokolShaderBuilder {
		ensureLive();
		if (viewSlot < 0 || samplerSlot < 0)
			throw "Sokol texture slots must be non-negative";
		SokolResult.check(NativeKitSokol.nks_shader_texture(value, viewSlot, samplerSlot, stage, name), "shader.texture");
		return this;
	}

	public function build():SokolShader {
		ensureLive();
		var made = NativeKitSokol.nks_shader_end(value);
		consumed = true;
		SokolResult.check(made.status, "shader.end");
		return new SokolShader(renderer, made.out_shader);
	}

	@:allow(SokolRenderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "Sokol shader builder has been consumed";
		renderer.ensureResourceOperation();
	}
}
