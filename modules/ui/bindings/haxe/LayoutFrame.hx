import NativeKitUI;

/** Per-submission viewport and pointer state for a LayoutSession. */
class LayoutFrame {
	var nativeFrame:nkui_layout_frame_input;
	public var width:Float;
	public var height:Float;
	public var pointerX:Float;
	public var pointerY:Float;
	public var pointerDown:Bool;
	public var deltaSeconds:Float;

	public function new(width:Float, height:Float) {
		if (width <= 0.0 || height <= 0.0)
			throw "Layout frame dimensions must be positive";
		this.width = width;
		this.height = height;
		pointerX = 0.0;
		pointerY = 0.0;
		pointerDown = false;
		deltaSeconds = 0.0;
		nativeFrame = new nkui_layout_frame_input();
	}

	public function setPointer(x:Float, y:Float, down:Bool):LayoutFrame {
		pointerX = x;
		pointerY = y;
		pointerDown = down;
		return this;
	}

	@:allow(LayoutSession)
	private function nativeValue():nkui_layout_frame_input {
		nativeFrame.set_struct_size(nkui_layout_frame_input.size());
		nativeFrame.set_width(width);
		nativeFrame.set_height(height);
		nativeFrame.set_pointer_x(pointerX);
		nativeFrame.set_pointer_y(pointerY);
		nativeFrame.set_pointer_down(pointerDown ? 1 : 0);
		nativeFrame.set_delta_seconds(deltaSeconds);
		return nativeFrame;
	}
}
