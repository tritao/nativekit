package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Horizontal linear gradient encoded as retained color bands. */
class GradientDecoration implements Decoration {
	public final start:Color;
	public final end:Color;
	public final bands:Int;

	public function new(start:Color, end:Color, bands:Int = 16) {
		if (start == null || end == null || bands <= 0)
			throw "Gradients require colors and positive bands";
		this.start = start;
		this.end = end;
		this.bands = bands;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var bandWidth = geometry.width / bands;
		for (band in 0...bands) {
			var amount = (band + 0.5) / bands;
			var color = Color.rgba(start.red + (end.red - start.red) * amount,
				start.green + (end.green - start.green) * amount,
				start.blue + (end.blue - start.blue) * amount,
				start.alpha + (end.alpha - start.alpha) * amount);
			canvas.fillRectIfPositive(new Rect(band * bandWidth, 0.0, bandWidth + 0.5,
				geometry.height), color);
		}
	}
}
