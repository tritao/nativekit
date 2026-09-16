package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Four-sided solid border using the existing retained custom-paint path. */
class BorderDecoration implements Decoration {
	public final color:Null<Color>;
	public final width:Null<Float>;

	public function new(?color:Color, ?width:Float) {
		this.color = color;
		this.width = width;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var borderColor = color == null ? style.get(StyleProperty.BorderColor) : color;
		var borderWidth = width == null ? style.get(StyleProperty.BorderWidth) : width;
		if (borderColor == null || borderWidth <= 0.0)
			return;
		var edge = Math.min(borderWidth, Math.min(geometry.width, geometry.height) * 0.5);
		canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, edge), borderColor);
		canvas.fillRectIfPositive(new Rect(0.0, geometry.height - edge, geometry.width, edge), borderColor);
		canvas.fillRectIfPositive(new Rect(0.0, edge, edge, geometry.height - 2.0 * edge), borderColor);
		canvas.fillRectIfPositive(new Rect(geometry.width - edge, edge, edge,
			geometry.height - 2.0 * edge), borderColor);
	}
}
