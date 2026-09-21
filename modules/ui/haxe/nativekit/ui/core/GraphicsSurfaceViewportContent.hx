package nativekit.ui.core;

import Canvas;
import GraphicsSurface;
import Rect;

/** Adapts a retained NativeKit GPU image/surface to a viewport content plane. */
class GraphicsSurfaceViewportContent implements ViewportContent {
	public final surface:GraphicsSurface;
	public final contentWidth:Float;
	public final contentHeight:Float;
	public var contentRevision(default, null):Int;

	public function new(surface:GraphicsSurface, width:Float, height:Float) {
		if (surface == null || surface.isDisposed() || width <= 0.0 || height <= 0.0 ||
			!finite(width) || !finite(height))
			throw "Graphics viewport content requires a live surface and positive dimensions";
		this.surface = surface;
		contentWidth = width;
		contentHeight = height;
		contentRevision = 1;
	}

	public function width():Float
		return contentWidth;

	public function height():Float
		return contentHeight;

	public function revision():Int
		return contentRevision;

	/** Marks the surface pixels as changed without replacing the GPU resource. */
	public function invalidate():Void
		contentRevision++;

	public function paint(canvas:Canvas, destination:Rect):Void {
		if (canvas == null || destination == null)
			throw "Graphics viewport painting requires a canvas and destination";
		canvas.drawSurface(surface, destination);
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
