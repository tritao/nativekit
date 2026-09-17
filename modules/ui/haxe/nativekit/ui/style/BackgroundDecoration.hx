package nativekit.ui.style;

import Canvas;
import Rect;
import ResolvedLayoutItem;

/** Paints the computed background as a custom decoration when native fill is unsuitable. */
class BackgroundDecoration extends Decoration {
	public final color:Null<Color>;

	public function new(?color:Color) {
		super(DecorationKind.Background);
		this.color = color == null ? null : Effect.copyColor(color);
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var resolved = color == null ? style.get(StyleProperty.Background) : color;
		canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height),
			resolved);
	}

	override public function copy():Decoration
		return new BackgroundDecoration(color);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		var value:BackgroundDecoration = cast other;
		return Effect.equalColor(color, value.color);
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration {
		if (other == null || other.kind != kind)
			return Decoration.discrete(this, other, amount);
		var value:BackgroundDecoration = cast other;
		if (color == null || value.color == null)
			return amount < 0.5 ? copy() : value.copy();
		return new BackgroundDecoration(Effect.interpolateColor(color, value.color, amount));
	}

	override public function describe():String
		return color == null ? "background(style)" :
			'background(${color.red},${color.green},${color.blue},${color.alpha})';
}
