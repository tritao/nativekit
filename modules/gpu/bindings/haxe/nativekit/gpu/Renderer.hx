package nativekit.gpu;
import NativeKitGpu;

import nativekit.gpu.Pipeline.PipelineBuilder;
import nativekit.gpu.Shader.ShaderBuilder;

/** Owns one renderer and all resources created through it. */
class Renderer {
	final surface:Surface;
	final resources:Array<Void->Void> = [];
	var value:nkgpu_renderer;
	var disposed:Bool = false;
	var frameActive:Bool = false;
	var passActive:Bool = false;

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

	/** Returns optional backend capabilities through the portable GPU envelope. */
	public function features():Features {
		ensureLive();
		var result = NativeKitGpu.nkgpu_query_features(value);
		GpuResult.check(result.status, "renderer.features");
		return Features.fromNative(result.out_features);
	}

	/** Returns portable resource and binding limits for this renderer. */
	public function limits():Limits {
		ensureLive();
		var result = NativeKitGpu.nkgpu_query_limits(value);
		GpuResult.check(result.status, "renderer.limits");
		return Limits.fromNative(result.out_limits);
	}

	/** Returns the portable operations supported by one image format. */
	public function imageFormatSupport(format:ImageFormat):ImageFormatSupport {
		ensureLive();
		var result = NativeKitGpu.nkgpu_query_image_format_support(value, format);
		GpuResult.check(result.status, "renderer.imageFormatSupport");
		return ImageFormatSupport.fromNative(result.out_support);
	}

	public function beginFrame():Void {
		ensureLive();
		if (frameActive)
			throw "GPU frame is already active";
		GpuResult.check(NativeKitGpu.nkgpu_begin_frame(value), "renderer.beginFrame");
		frameActive = true;
		passActive = true;
	}

	public function endFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "GPU frame is not active";
		GpuResult.check(NativeKitGpu.nkgpu_end_frame(value), "renderer.endFrame");
		frameActive = false;
		passActive = false;
	}

	/** Begins a frame that will contain explicit render, compute, or copy passes. */
	public function beginPassFrame():Void {
		ensureLive();
		if (frameActive)
			throw "GPU frame is already active";
		GpuResult.check(NativeKitGpu.nkgpu_frame_begin(value), "renderer.beginPassFrame");
		frameActive = true;
		passActive = false;
	}

	/** Begins a generic render pass inside an explicit pass frame. */
	public function beginRenderPass(desc:RenderPassDesc):Void {
		ensureLive();
		if (!frameActive || passActive)
			throw "GPU render pass requires a frame without an active pass";
		if (desc == null)
			throw "GPU render-pass descriptor must not be null";
		GpuResult.check(NativeKitGpu.nkgpu_begin_render_pass(value, desc.nativeValue()),
			"renderer.beginRenderPass");
		passActive = true;
	}

	/** Begins a compute pass inside an explicit pass frame. */
	public function beginComputePass():Void {
		ensureLive();
		if (!frameActive || passActive)
			throw "GPU compute pass requires a frame without an active pass";
		GpuResult.check(NativeKitGpu.nkgpu_begin_compute_pass(value), "renderer.beginComputePass");
		passActive = true;
	}

	/** Begins a transfer pass inside an explicit pass frame. */
	public function beginCopyPass():Void {
		ensureLive();
		if (!frameActive || passActive)
			throw "GPU copy pass requires a frame without an active pass";
		GpuResult.check(NativeKitGpu.nkgpu_begin_copy_pass(value), "renderer.beginCopyPass");
		passActive = true;
	}

	/** Ends the active generic render, compute, or copy pass. */
	public function endPass():Void {
		ensureFrame();
		if (!passActive)
			throw "GPU pass is not active";
		GpuResult.check(NativeKitGpu.nkgpu_end_pass(value), "renderer.endPass");
		passActive = false;
	}

	/** Dispatches compute workgroups in the active compute pass. */
	public function dispatch(x:Int, y:Int = 1, z:Int = 1):Void {
		ensureFrame();
		if (x <= 0 || y <= 0 || z <= 0)
			throw "GPU dispatch dimensions must be positive";
		GpuResult.check(NativeKitGpu.nkgpu_dispatch(value, x, y, z), "renderer.dispatch");
	}

	/** Copies a byte range between buffers in the active copy pass. */
	public function copyBuffer(source:Buffer, destination:Buffer, size:Int,
		sourceOffset:Int = 0, destinationOffset:Int = 0):Void {
		ensureFrame();
		source.ensureLive();
		destination.ensureLive();
		if (source.rendererOwner() != this || destination.rendererOwner() != this || size <= 0 ||
			sourceOffset < 0 || destinationOffset < 0)
			throw "GPU buffer-copy arguments are invalid";
		var desc = new nkgpu_buffer_copy_desc();
		desc.set_struct_size(24);
		desc.set_source(source.nativeHandle());
		desc.set_source_offset(sourceOffset);
		desc.set_destination(destination.nativeHandle());
		desc.set_destination_offset(destinationOffset);
		desc.set_size(size);
		GpuResult.check(NativeKitGpu.nkgpu_buffer_copy(value, desc), "renderer.copyBuffer");
	}

	/** Copies a 2D image region in the active copy pass. */
	public function copyImage(source:Image, destination:Image, width:Int, height:Int,
		sourceX:Int = 0, sourceY:Int = 0, destinationX:Int = 0, destinationY:Int = 0):Void {
		ensureFrame();
		source.ensureLive();
		destination.ensureLive();
		if (source.rendererOwner() != this || destination.rendererOwner() != this || width <= 0 ||
			height <= 0 || sourceX < 0 || sourceY < 0 || destinationX < 0 || destinationY < 0)
			throw "GPU image-copy arguments are invalid";
		var desc = new nkgpu_image_copy_desc();
		desc.set_struct_size(52);
		desc.set_source(source.nativeHandle());
		desc.set_source_mip(0);
		desc.set_source_layer(0);
		desc.set_source_x(sourceX);
		desc.set_source_y(sourceY);
		desc.set_destination(destination.nativeHandle());
		desc.set_destination_mip(0);
		desc.set_destination_layer(0);
		desc.set_destination_x(destinationX);
		desc.set_destination_y(destinationY);
		desc.set_width(width);
		desc.set_height(height);
		GpuResult.check(NativeKitGpu.nkgpu_image_copy(value, desc), "renderer.copyImage");
	}

	/** Uploads a buffer rectangle into an image in the active copy pass. */
	public function bufferToImage(source:Buffer, destination:Image, width:Int, height:Int,
		rowPitch:Int = 0, bufferOffset:Int = 0, x:Int = 0, y:Int = 0,
		mipLevel:Int = 0, layer:Int = 0):Void {
		ensureFrame();
		source.ensureLive();
		destination.ensureLive();
		if (source.rendererOwner() != this || destination.rendererOwner() != this || width <= 0 ||
			height <= 0 || rowPitch < 0 || bufferOffset < 0 || x < 0 || y < 0 ||
			mipLevel < 0 || layer < 0)
			throw "GPU buffer-to-image arguments are invalid";
		var desc = new nkgpu_buffer_image_copy_desc();
		desc.set_struct_size(44);
		desc.set_buffer(source.nativeHandle());
		desc.set_buffer_offset(bufferOffset);
		desc.set_row_pitch(rowPitch);
		desc.set_image(destination.nativeHandle());
		desc.set_mip_level(mipLevel);
		desc.set_layer(layer);
		desc.set_x(x);
		desc.set_y(y);
		desc.set_width(width);
		desc.set_height(height);
		GpuResult.check(NativeKitGpu.nkgpu_buffer_to_image(value, desc), "renderer.bufferToImage");
	}

	/** Downloads an image rectangle into a buffer in top-to-bottom row order. */
	public function imageToBuffer(source:Image, destination:Buffer, width:Int, height:Int,
		rowPitch:Int = 0, bufferOffset:Int = 0, x:Int = 0, y:Int = 0,
		mipLevel:Int = 0, layer:Int = 0):Void {
		ensureFrame();
		source.ensureLive();
		destination.ensureLive();
		if (source.rendererOwner() != this || destination.rendererOwner() != this || width <= 0 ||
			height <= 0 || rowPitch < 0 || bufferOffset < 0 || x < 0 || y < 0 ||
			mipLevel < 0 || layer < 0)
			throw "GPU image-to-buffer arguments are invalid";
		var desc = new nkgpu_buffer_image_copy_desc();
		desc.set_struct_size(44);
		desc.set_buffer(destination.nativeHandle());
		desc.set_buffer_offset(bufferOffset);
		desc.set_row_pitch(rowPitch);
		desc.set_image(source.nativeHandle());
		desc.set_mip_level(mipLevel);
		desc.set_layer(layer);
		desc.set_x(x);
		desc.set_y(y);
		desc.set_width(width);
		desc.set_height(height);
		GpuResult.check(NativeKitGpu.nkgpu_image_to_buffer(value, desc), "renderer.imageToBuffer");
	}

	/** Applies a framebuffer-pixel viewport to the active render pass. */
	public function viewport(x:Int, y:Int, width:Int, height:Int):Void {
		ensureFrame();
		if (width <= 0 || height <= 0)
			throw "GPU viewport dimensions must be positive";
		GpuResult.check(NativeKitGpu.nkgpu_apply_viewport(value, x, y, width, height), "renderer.viewport");
	}

	/** Applies or disables a framebuffer-pixel scissor rectangle. */
	public function scissor(enabled:Bool, x:Int = 0, y:Int = 0, width:Int = 0,
		height:Int = 0):Void {
		ensureFrame();
		if (enabled && (width <= 0 || height <= 0))
			throw "GPU scissor dimensions must be positive when enabled";
		GpuResult.check(NativeKitGpu.nkgpu_apply_scissor(value, enabled ? 1 : 0, x, y, width, height),
			"renderer.scissor");
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

	public function batch():Batch
		return Batch.begin(this);

	@:allow(SurfaceFrame)
	function ownsSurface(surface:Surface):Bool
		return this.surface == surface;

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

	@:allow(Buffer, Image, Sampler, Shader, Pipeline, Readback, Timestamp, Batch, CommandBuffer, Uniforms,
		SurfaceFrame)
	function ensureFrame():Void {
		ensureLive();
		if (!frameActive)
			throw "GPU operation requires an active frame";
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, Readback, Timestamp, Batch,
		CommandBuffer, Uniforms, SurfaceFrame)
	function ensureLive():Void {
		if (disposed)
			throw "GPU renderer has been disposed";
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, Readback, Timestamp, Batch,
		Uniforms, SurfaceFrame)
	function registerResource(release:Void->Void):Void {
		ensureLive();
		resources.push(release);
	}

	@:allow(Buffer, Image, Sampler, Shader, ShaderBuilder, Pipeline, PipelineBuilder, Readback, Batch,
		Uniforms, SurfaceFrame)
	function ensureResourceOperation():Void {
		ensureLive();
		if (frameActive)
			throw "GPU resources can only change while the renderer is idle";
	}
}
