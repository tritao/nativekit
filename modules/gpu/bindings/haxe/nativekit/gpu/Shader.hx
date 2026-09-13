package nativekit.gpu;

import nativekit.gpu.Enums.ShaderStage;
import nativekit.gpu.Enums.ShaderLanguage;
import nativekit.gpu.Enums.UniformType;

/** Renderer-owned shader resource. */
class Shader {
	final renderer:Renderer;
	final value:nkgpu_shader;
	var disposed:Bool = false;

	@:allow(ShaderBuilder)
	private function new(renderer:Renderer, value:nkgpu_shader) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function create(renderer:Renderer, language:ShaderLanguage,
		vertexSource:String, fragmentSource:String):Shader {
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_shader_create(renderer.nativeHandle(), language,
			vertexSource, fragmentSource);
		GpuResult.check(made.status, "shader.create");
		return new Shader(renderer, made.out_shader);
	}

	public static function begin(renderer:Renderer, language:ShaderLanguage,
		vertexSource:String, fragmentSource:String):ShaderBuilder {
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_shader_begin(renderer.nativeHandle(), language,
			vertexSource, fragmentSource);
		GpuResult.check(made.status, "shader.begin");
		return new ShaderBuilder(renderer, made.out_builder);
	}

	public function nativeHandle():nkgpu_shader {
		ensureLive();
		return value;
	}

	@:allow(Pipeline)
	function rendererOwner():Renderer
		return renderer;

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_shader_destroy(renderer.nativeHandle(), value), "shader.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "GPU shader has been disposed";
		renderer.ensureLive();
	}
}

/** Configures shader bindings before creating the immutable shader resource. */
class ShaderBuilder {
	final renderer:Renderer;
	final value:nkgpu_shader_builder;
	var consumed:Bool = false;

	@:allow(Shader)
	private function new(renderer:Renderer, value:nkgpu_shader_builder) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public function uniformBlock(slot:Int, stage:ShaderStage, size:Int):ShaderBuilder {
		ensureLive();
		if (slot < 0 || size <= 0)
			throw "GPU uniform block slot and size are invalid";
		GpuResult.check(NativeKitGpu.nkgpu_shader_uniform_block(value, slot, stage, size), "shader.uniformBlock");
		return this;
	}

	public function uniform(slot:Int, memberIndex:Int, name:String, type:UniformType, arrayCount:Int = 1):ShaderBuilder {
		ensureLive();
		if (slot < 0 || memberIndex < 0 || arrayCount < 0)
			throw "GPU uniform description is invalid";
		GpuResult.check(NativeKitGpu.nkgpu_shader_uniform(value, slot, memberIndex, name, type, arrayCount), "shader.uniform");
		return this;
	}

	public function texture(viewSlot:Int, samplerSlot:Int, stage:ShaderStage, name:String):ShaderBuilder {
		ensureLive();
		if (viewSlot < 0 || samplerSlot < 0)
			throw "GPU texture slots must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_shader_texture(value, viewSlot, samplerSlot, stage, name), "shader.texture");
		return this;
	}

	public function build():Shader {
		ensureLive();
		var made = NativeKitGpu.nkgpu_shader_end(value);
		consumed = true;
		GpuResult.check(made.status, "shader.end");
		return new Shader(renderer, made.out_shader);
	}

	@:allow(Renderer)
	function rendererClosed():Void
		consumed = true;

	function ensureLive():Void {
		if (consumed)
			throw "GPU shader builder has been consumed";
		renderer.ensureResourceOperation();
	}
}
