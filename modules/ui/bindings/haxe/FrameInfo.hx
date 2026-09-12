import NativeKitUI;

/** Per-frame logical and physical surface dimensions. */
class FrameInfo {
	var nativeFrame:nkui_frame_info;
	public var logicalWidth:Float;
	public var logicalHeight:Float;
	public var framebufferWidth:Int;
	public var framebufferHeight:Int;
	public var pixelScale:Float;

	public function new(logicalWidth:Float, logicalHeight:Float, framebufferWidth:Int, framebufferHeight:Int, pixelScale:Float) {
		if (logicalWidth <= 0.0 || logicalHeight <= 0.0 || framebufferWidth <= 0 || framebufferHeight <= 0 || pixelScale <= 0.0)
			throw "Frame dimensions and pixel scale must be positive";
		this.logicalWidth = logicalWidth;
		this.logicalHeight = logicalHeight;
		this.framebufferWidth = framebufferWidth;
		this.framebufferHeight = framebufferHeight;
		this.pixelScale = pixelScale;
		nativeFrame = new nkui_frame_info();
	}

	/** Updates this reusable frame description without allocating a new ABI struct. */
	public function set(logicalWidth:Float, logicalHeight:Float, framebufferWidth:Int,
			framebufferHeight:Int, pixelScale:Float):FrameInfo {
		if (logicalWidth <= 0.0 || logicalHeight <= 0.0 || framebufferWidth <= 0 ||
				framebufferHeight <= 0 || pixelScale <= 0.0)
			throw "Frame dimensions and pixel scale must be positive";
		this.logicalWidth = logicalWidth;
		this.logicalHeight = logicalHeight;
		this.framebufferWidth = framebufferWidth;
		this.framebufferHeight = framebufferHeight;
		this.pixelScale = pixelScale;
		return this;
	}

	@:allow(Renderer)
	@:allow(LayoutSession)
	private function nativeValue():nkui_frame_info {
		nativeFrame.set_struct_size(nkui_frame_info.size());
		nativeFrame.set_logical_width(logicalWidth);
		nativeFrame.set_logical_height(logicalHeight);
		nativeFrame.set_framebuffer_width(framebufferWidth);
		nativeFrame.set_framebuffer_height(framebufferHeight);
		nativeFrame.set_pixel_scale(pixelScale);
		return nativeFrame;
	}
}
