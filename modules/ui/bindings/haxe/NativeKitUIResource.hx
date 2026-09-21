import NativeKitUI;

/** Common lifetime implementation for typed UI resources. */
class NativeKitUIResource {
	var value:nkui_resource;
	var disposed:Bool;

	function new(value:nkui_resource) {
		this.value = value;
		disposed = false;
	}

	/** Releases this resource. Repeated disposal is safe. */
	public function dispose():Void {
		if (disposed)
			return;
		var status = NativeKitUI.nkui_resource_destroy(value);
		disposed = true;
		UiResult.check(status, "resource.dispose");
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(CanvasCommandBuffer)
	@:allow(Canvas)
	@:allow(DisplayList)
	@:allow(FontCollection)
	@:allow(GraphicsSurface)
	@:allow(TextLayout)
	@:allow(LayoutSession)
	private function nativeHandle():nkui_resource {
		if (disposed)
			throw "NativeKit UI resource has been disposed";
		return value;
	}
}
