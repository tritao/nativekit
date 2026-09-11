import NativeKitUI;

/** Per-frame logical and physical surface dimensions. */
class FrameInfo {
	public final logicalWidth:Float;
	public final logicalHeight:Float;
	public final framebufferWidth:Int;
	public final framebufferHeight:Int;
	public final pixelScale:Float;

	public function new(logicalWidth:Float, logicalHeight:Float, framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float) {
		if (logicalWidth <= 0.0 || logicalHeight <= 0.0 || framebufferWidth <= 0 || framebufferHeight <= 0 || pixelScale <= 0.0)
			throw "Frame dimensions and pixel scale must be positive";
		this.logicalWidth = logicalWidth;
		this.logicalHeight = logicalHeight;
		this.framebufferWidth = framebufferWidth;
		this.framebufferHeight = framebufferHeight;
		this.pixelScale = pixelScale;
	}

	@:allow(Renderer)
	@:allow(LayoutSession)
	private function nativeValue():nkui_frame_info {
		var result = new nkui_frame_info();
		result.set_struct_size(nkui_frame_info.size());
		result.set_logical_width(logicalWidth);
		result.set_logical_height(logicalHeight);
		result.set_framebuffer_width(framebufferWidth);
		result.set_framebuffer_height(framebufferHeight);
		result.set_pixel_scale(pixelScale);
		return result;
	}
}
