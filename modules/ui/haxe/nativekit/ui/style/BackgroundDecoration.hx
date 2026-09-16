package nativekit.ui.style;

import Canvas;
import Rect;
import ResolvedLayoutItem;

/** Paints the computed background as a custom decoration when native fill is unsuitable. */
class BackgroundDecoration implements Decoration {
	public function new() {}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void
		canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height),
			style.get(StyleProperty.Background));
}
