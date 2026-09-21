import Canvas;
import DisplayList;
import Rect;

/** Paint callback for a retained custom layout content provider. */
typedef LayoutContentPaint = (canvas:Canvas, geometry:ResolvedLayoutItem) -> Void;

/**
 * Pairs intrinsic measurement with a retained Canvas display list.
 *
 * The painter draws in the custom node's local layout space. LayoutSession
 * applies the resolved position, transform, and ancestor clipping when the
 * list is embedded, matching the framework RenderNode paint contract. The
 * display list is regenerated only when the provider version or local size
 * changes.
 */
class LayoutRenderableContent implements LayoutContent {
	final measurement:LayoutContent;
	final painter:LayoutContentPaint;
	final canvas:Canvas;
	final list:DisplayList;
	var paintedVersion:Int;
	var paintedGeometry:Null<ResolvedLayoutItem>;

	public function new(measurement:LayoutContent, painter:LayoutContentPaint,
			canvasCapacity:Int = 4096) {
		if (measurement == null || painter == null)
			throw "Renderable content requires measurement and paint callbacks";
		this.measurement = measurement;
		this.painter = painter;
		canvas = new Canvas(canvasCapacity);
		list = DisplayList.create();
		paintedVersion = -1;
		paintedGeometry = null;
	}

	public function getVersion():Int
		return measurement.getVersion();

	public function measure(constraints:LayoutMeasureConstraints):LayoutMeasureResult
		return measurement.measure(constraints);

	/** Rebuilds and returns the retained display list when its cache key changed. */
	public function paint(geometry:ResolvedLayoutItem):DisplayList {
		if (geometry == null)
			throw "Renderable content requires resolved geometry";
		var version = getVersion();
		if (paintedGeometry != null && paintedVersion == version &&
			geometryEqual(paintedGeometry, geometry))
			return list;

		canvas.reset();
		canvas.withState(function(target) {
			target.resetTransform();
			target.clip(new Rect(0.0, 0.0, geometry.width, geometry.height));
			painter(target, geometry);
		});
		list.update(canvas);
		paintedVersion = version;
		paintedGeometry = geometry;
		return list;
	}

	/** Clears the retained command stream; the next layout render repaints it. */
	public function clear():Void {
		canvas.reset();
		list.clear();
		paintedVersion = -1;
		paintedGeometry = null;
	}

	/** Releases the retained display list and its referenced graphics resources. */
	public function dispose():Void {
		canvas.reset();
		list.dispose();
		paintedGeometry = null;
	}

	static function geometryEqual(a:ResolvedLayoutItem, b:ResolvedLayoutItem):Bool {
		return a.width == b.width && a.height == b.height;
	}
}
