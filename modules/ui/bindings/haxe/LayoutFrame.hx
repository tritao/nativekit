import NativeKitUI;

/** Per-submission viewport and timing for a LayoutSession. */
class LayoutFrame {
	var nativeFrame:nkui_layout_frame_input;
	public var width:Float;
	public var height:Float;
	public var deltaSeconds:Float;

	public function new(width:Float, height:Float) {
		if (width <= 0.0 || height <= 0.0)
			throw "Layout frame dimensions must be positive";
		this.width = width;
		this.height = height;
		deltaSeconds = 0.0;
		nativeFrame = new nkui_layout_frame_input();
	}

	public function setViewport(width:Float, height:Float):LayoutFrame {
		if (width <= 0.0 || height <= 0.0)
			throw "Layout frame dimensions must be positive";
		this.width = width;
		this.height = height;
		return this;
	}

	@:allow(LayoutSession)
	private function nativeValue():nkui_layout_frame_input {
		nativeFrame.set_struct_size(nkui_layout_frame_input.size());
		nativeFrame.set_width(width);
		nativeFrame.set_height(height);
		nativeFrame.set_delta_seconds(deltaSeconds);
		return nativeFrame;
	}
}
