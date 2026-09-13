import haxe.io.Bytes;

/** Renderer-owned RGBA8 sampled image. */
class SokolImage {
	final renderer:SokolRenderer;
	final value:nks_image;
	public final width:Int;
	public final height:Int;
	var disposed:Bool = false;

	private function new(renderer:SokolRenderer, value:nks_image, width:Int, height:Int) {
		this.renderer = renderer;
		this.value = value;
		this.width = width;
		this.height = height;
		renderer.registerResource(rendererClosed);
	}

	/** Copies tightly packed row-major RGBA8 pixels into a new GPU image. */
	public static function fromRgba8(renderer:SokolRenderer, width:Int, height:Int, pixels:Bytes):SokolImage {
		if (width <= 0 || height <= 0 || pixels == null || width > Std.int(536870911 / height)
			|| pixels.length != width * height * 4)
			throw "Sokol RGBA8 image dimensions or pixel storage are invalid";
		var madeBuilder = NativeKitSokol.nks_image_begin(renderer.nativeHandle(), width, height);
		SokolResult.check(madeBuilder.status, "image.begin");
		var builder = madeBuilder.out_builder;
		for (y in 0...height)
			for (x in 0...width) {
				var offset = (y * width + x) * 4;
				SokolResult.check(NativeKitSokol.nks_image_write_rgba8(builder, x, y, pixels.get(offset), pixels.get(offset + 1),
					pixels.get(offset + 2), pixels.get(offset + 3)), "image.writePixel");
			}
		var made = NativeKitSokol.nks_image_end(builder);
		SokolResult.check(made.status, "image.end");
		return new SokolImage(renderer, made.out_image, width, height);
	}

	public function nativeHandle():nks_image {
		ensureLive();
		return value;
	}

	public function apply(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "Sokol image slot must be non-negative";
		SokolResult.check(NativeKitSokol.nks_apply_image(renderer.nativeHandle(), slot, value), "image.apply");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		SokolResult.check(NativeKitSokol.nks_image_destroy(renderer.nativeHandle(), value), "image.dispose");
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
			throw "Sokol image has been disposed";
		renderer.ensureResourceOperation();
	}
}
