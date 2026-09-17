package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Paints a focus/accessibility outline just inside the node clip. */
class OutlineDecoration extends Decoration {
	public final color:Null<Color>;
	public final width:Null<Float>;

	public function new(?color:Color, ?width:Float) {
		super(DecorationKind.Outline);
		this.color = color;
		this.width = width;
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
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
		canvas.fillRectIfPositive(new Rect(geometry.width - edge - edge, bounds.y + edge,
			edge, bounds.height - 2.0 * edge), outlineColor);
	}

	override public function copy():Decoration
		return new OutlineDecoration(color, width);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		var value:OutlineDecoration = cast other;
		return Effect.equalColor(color, value.color) && width == value.width;
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration {
		if (other == null || other.kind != kind)
			return Decoration.discrete(this, other, amount);
		var value:OutlineDecoration = cast other;
		if (color == null || value.color == null || width == null || value.width == null)
			return amount < 0.5 ? copy() : value.copy();
		return new OutlineDecoration(Effect.interpolateColor(color, value.color, amount),
			width + (value.width - width) * amount);
	}

	override public function describe():String
		return 'outline(${color == null ? "style" : color.red + "," + color.green + "," +
			color.blue + "," + color.alpha},${width == null ? "style" : width})';
}
