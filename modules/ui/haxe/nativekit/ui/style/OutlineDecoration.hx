package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Paints a focus/accessibility outline just inside the node clip. */
class OutlineDecoration implements Decoration {
	public final color:Null<Color>;
	public final width:Null<Float>;

	public function new(?color:Color, ?width:Float) {
		this.color = color;
		this.width = width;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var outlineColor = color == null ? style.get(StyleProperty.OutlineColor) : color;
		var outlineWidth = width == null ? style.get(StyleProperty.OutlineWidth) : width;
		if (outlineColor == null || outlineWidth <= 0.0)
			return;
		var edge = Math.min(outlineWidth, Math.min(geometry.width, geometry.height) * 0.5);
		var bounds = new Rect(edge, edge, geometry.width - 2.0 * edge, geometry.height - 2.0 * edge);
		canvas.fillRectIfPositive(new Rect(bounds.x, bounds.y, bounds.width, edge), outlineColor);
		canvas.fillRectIfPositive(new Rect(bounds.x, geometry.height - edge - edge,
			bounds.width, edge), outlineColor);
		canvas.fillRectIfPositive(new Rect(bounds.x, bounds.y + edge, edge,
			bounds.height - 2.0 * edge), outlineColor);
		canvas.fillRectIfPositive(new Rect(geometry.width - edge - edge, bounds.y + edge, edge,
			bounds.height - 2.0 * edge), outlineColor);
	}
}
