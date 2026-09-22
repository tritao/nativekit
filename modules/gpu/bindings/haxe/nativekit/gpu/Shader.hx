package nativekit.gpu;
import NativeKitGpu;

import nativekit.gpu.Enums.ShaderStage;
import nativekit.gpu.Enums.ShaderLanguage;
import nativekit.gpu.Enums.ImageFormat;
import nativekit.gpu.Enums.ImageType;
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

	/** Begins a compute-only shader builder. */
	public static function beginCompute(renderer:Renderer, language:ShaderLanguage,
		computeSource:String):ShaderBuilder {
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_shader_begin_compute(renderer.nativeHandle(), language, computeSource);
		GpuResult.check(made.status, "shader.beginCompute");
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

	public function attribute(location:Int, glslName:String, hlslSemantic:String,
		semanticIndex:Int = 0):ShaderBuilder {
		ensureLive();
		if (location < 0 || semanticIndex < 0)
			throw "GPU shader vertex attribute is invalid";
		GpuResult.check(NativeKitGpu.nkgpu_shader_attribute(value, location, glslName,
			hlslSemantic, semanticIndex), "shader.attribute");
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

	/** Describes a filtering texture binding with an explicit image shape. */
	public function textureType(viewSlot:Int, samplerSlot:Int, stage:ShaderStage,
		imageType:ImageType, name:String):ShaderBuilder {
		ensureLive();
		if (viewSlot < 0 || samplerSlot < 0)
			throw "GPU texture slots must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_shader_texture_type(value, viewSlot, samplerSlot, stage,
			imageType, name), "shader.textureType");
		return this;
	}

	/** Describes a storage-buffer resource for a compute or graphics stage. */
	public function storageBuffer(viewSlot:Int, stage:ShaderStage = ShaderStage.Compute,
		readonly:Bool = false):ShaderBuilder {
		ensureLive();
		if (viewSlot < 0)
			throw "GPU storage-buffer slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_shader_storage_buffer(value, viewSlot, stage,
			readonly ? 1 : 0), "shader.storageBuffer");
		return this;
	}

	/** Describes a compute storage-image resource and its access format. */
	public function storageImage(viewSlot:Int, format:ImageFormat,
		writeonly:Bool = false):ShaderBuilder {
		ensureLive();
		if (viewSlot < 0)
			throw "GPU storage-image slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_shader_storage_image(value, viewSlot, format,
			writeonly ? 1 : 0), "shader.storageImage");
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
