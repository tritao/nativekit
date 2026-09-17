package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Four-sided solid border using the existing retained custom-paint path. */
class BorderDecoration extends Decoration {
	public final color:Null<Color>;
	public final width:Null<Float>;

	public function new(?color:Color, ?width:Float) {
		super(DecorationKind.Border);
		this.color = color;
		this.width = width;
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
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

	override public function copy():Decoration
		return new BorderDecoration(color, width);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		var value:BorderDecoration = cast other;
		return Effect.equalColor(color, value.color) && width == value.width;
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration {
		if (other == null || other.kind != kind)
			return Decoration.discrete(this, other, amount);
		var value:BorderDecoration = cast other;
		if (color == null || value.color == null || width == null || value.width == null)
			return amount < 0.5 ? copy() : value.copy();
		return new BorderDecoration(Effect.interpolateColor(color, value.color, amount),
			width + (value.width - width) * amount);
	}

	override public function describe():String
		return 'border(${color == null ? "style" : color.red + "," + color.green + "," +
			color.blue + "," + color.alpha},${width == null ? "style" : width})';
}
