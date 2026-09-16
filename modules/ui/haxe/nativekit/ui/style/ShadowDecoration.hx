package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Lightweight rectangular shadow; blur is represented by concentric alpha bands. */
class ShadowDecoration implements Decoration {
	public final color:Null<Color>;
	public final offsetX:Null<Float>;
	public final offsetY:Null<Float>;
	public final blur:Null<Float>;

	public function new(?color:Color, ?offsetX:Float, ?offsetY:Float, ?blur:Float) {
		this.color = color;
		this.offsetX = offsetX;
		this.offsetY = offsetY;
		this.blur = blur;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var shadowColor = color == null ? style.get(StyleProperty.ShadowColor) : color;
		var x = offsetX == null ? style.get(StyleProperty.ShadowOffsetX) : offsetX;
		var y = offsetY == null ? style.get(StyleProperty.ShadowOffsetY) : offsetY;
		var radius = blur == null ? style.get(StyleProperty.ShadowBlur) : blur;
		if (shadowColor == null || shadowColor.alpha <= 0.0)
			return;
		var bands = radius <= 0.0 ? 1 : 4;
		for (band in 0...bands) {
			var progress = (band + 1) / bands;
			var spread = radius * progress;
			var alpha = shadowColor.alpha * (1.0 - progress) / bands;
			var bandColor = Color.rgba(shadowColor.red, shadowColor.green, shadowColor.blue, alpha);
			canvas.fillRectIfPositive(new Rect(x - spread, y - spread,
				geometry.width + 2.0 * spread, geometry.height + 2.0 * spread), bandColor);
		}
	}
}
