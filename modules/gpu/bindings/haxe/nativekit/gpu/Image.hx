package nativekit.gpu;

import nativekit.gpu.GpuResult;
import nativekit.ffi.NativeKitGpu;
import nativekit.ffi.NativeKitTypes;

import haxe.io.Bytes;
import GraphicsImageRef;

/** Renderer-owned RGBA8 sampled image. */
class Image {
	final renderer:Renderer;
	final value:nkgpu_image;
	public final width:Int;
	public final height:Int;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_image, width:Int, height:Int) {
		this.renderer = renderer;
		this.value = value;
		this.width = width;
		this.height = height;
		renderer.registerResource(rendererClosed);
	}

	/** Creates an image from the current generic image descriptor. */
	public static function create(renderer:Renderer, desc:ImageDesc):Image {
		if (desc == null || desc.width <= 0 || desc.height <= 0 || desc.mipCount <= 0 ||
			desc.sampleCount <= 0 || desc.layerCount <= 0)
			throw "GPU image descriptor dimensions and counts must be positive";
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_image_create_desc(renderer.nativeHandle(), desc.nativeValue());
		GpuResult.check(made.status, "image.create");
		return new Image(renderer, made.out_image, desc.width, desc.height);
	}

	/** Copies tightly packed row-major RGBA8 pixels into a new GPU image. */
	public static function fromRgba8(renderer:Renderer, width:Int, height:Int, pixels:Bytes):Image {
		if (width <= 0 || height <= 0 || pixels == null || width > Std.int(536870911 / height)
			|| pixels.length != width * height * 4)
			throw "GPU RGBA8 image dimensions or pixel storage are invalid";
		renderer.ensureResourceOperation();
		var madeBuilder = NativeKitGpu.nkgpu_image_begin(renderer.nativeHandle(), width, height);
		GpuResult.check(madeBuilder.status, "image.begin");
		var builder = madeBuilder.out_builder;
		for (y in 0...height)
			for (x in 0...width) {
				var offset = (y * width + x) * 4;
				GpuResult.check(NativeKitGpu.nkgpu_image_write_rgba8(builder, x, y, pixels.get(offset), pixels.get(offset + 1),
					pixels.get(offset + 2), pixels.get(offset + 3)), "image.writePixel");
			}
		var made = NativeKitGpu.nkgpu_image_end(builder);
		GpuResult.check(made.status, "image.end");
		return new Image(renderer, made.out_image, width, height);
	}

	public function nativeHandle():nkgpu_image {
		ensureLive();
		return value;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "GPU image slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_image(renderer.nativeHandle(), slot, value), "image.apply");
	}

	/** Binds this image as a storage resource in the active pass. */
	public function applyStorage(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "GPU storage-image slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_storage_image(renderer.nativeHandle(), slot, value),
			"image.applyStorage");
	}

	/** Exports this sampled image as a retained cross-module graphics image. */
	public function graphicsImage():GraphicsImageRef {
		ensureLive();
		var borrowed = NativeKitGpu.nkgpu_image_get_graphics_image(renderer.nativeHandle(), value);
		GpuResult.check(borrowed.status, "image.graphicsImage");
		var api:GraphicsApi = NativeKitGpu.nkgpu_query_graphics_api(renderer.nativeHandle());
		return GraphicsImageRef.fromBorrowedHandle(borrowed.out_image, width, height, api);
	}

	/** Updates a tightly packed image rectangle. */
	public function update(x:Int, y:Int, width:Int, height:Int, pixels:Bytes, rowPitch:Int):Void {
		ensureLive();
		if (x < 0 || y < 0 || width <= 0 || height <= 0 || pixels == null || rowPitch <= 0)
			throw "GPU image update arguments are invalid";
		GpuResult.check(NativeKitGpu.nkgpu_image_update(renderer.nativeHandle(), value, x, y, width,
			height, pixels, rowPitch), "image.update");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_image_destroy(renderer.nativeHandle(), value), "image.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(CommandBuffer, Renderer, Readback)
	function rendererOwner():Renderer
		return renderer;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	@:allow(Renderer)
	function ensureLive():Void {
		if (disposed)
			throw "GPU image has been disposed";
		renderer.ensureLive();
	}
}
