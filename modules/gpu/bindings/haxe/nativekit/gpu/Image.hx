package nativekit.gpu;

import haxe.io.Bytes;

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

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_image_destroy(renderer.nativeHandle(), value), "image.dispose");
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
			throw "GPU image has been disposed";
		renderer.ensureLive();
	}
}
