package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Geometry-based rounded-rectangle shadow; distinct from subtree DropShadowEffect. */
class ShadowDecoration implements Decoration {
	public final color:Null<Color>;
	public final offsetX:Null<Float>;
	public final offsetY:Null<Float>;
	public final blur:Null<Float>;
	public final spread:Null<Float>;

	public function new(?color:Color, ?offsetX:Float, ?offsetY:Float, ?blur:Float, ?spread:Float) {
		this.color = color;
		this.offsetX = offsetX;
		this.offsetY = offsetY;
		this.blur = blur;
		this.spread = spread;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var shadowColor = color == null ? style.get(StyleProperty.ShadowColor) : color;
		var x = offsetX == null ? style.get(StyleProperty.ShadowOffsetX) : offsetX;
		var y = offsetY == null ? style.get(StyleProperty.ShadowOffsetY) : offsetY;
		var radius = blur == null ? style.get(StyleProperty.ShadowBlur) : blur;
		if (shadowColor == null || shadowColor.alpha <= 0.0)
			return;
		var topLeft = style.get(StyleProperty.RadiusTopLeft);
		var topRight = style.get(StyleProperty.RadiusTopRight);
		var bottomRight = style.get(StyleProperty.RadiusBottomRight);
		var bottomLeft = style.get(StyleProperty.RadiusBottomLeft);
		var resolvedSpread = spread == null ? 0.0 : spread;
		canvas.drawBoxShadow(new Rect(0.0, 0.0, geometry.width, geometry.height), x, y, radius,
			resolvedSpread, [topLeft, topRight, bottomRight, bottomLeft], shadowColor);
	}
}
