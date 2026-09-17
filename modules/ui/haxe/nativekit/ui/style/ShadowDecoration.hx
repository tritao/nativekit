package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Geometry-based rounded-rectangle shadow; distinct from subtree DropShadowEffect. */
class ShadowDecoration extends Decoration {
	public final color:Null<Color>;
	public final offsetX:Null<Float>;
	public final offsetY:Null<Float>;
	/** Optional UI-facing blur radius override. */
	public final blurRadius:Null<Float>;
	public final spread:Null<Float>;

	public function new(?color:Color, ?offsetX:Float, ?offsetY:Float, ?blurRadius:Float, ?spread:Float) {
		super(DecorationKind.Shadow);
		this.color = color;
		this.offsetX = offsetX;
		this.offsetY = offsetY;
		this.blurRadius = blurRadius;
		this.spread = spread;
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		var shadowColor = color == null ? style.get(StyleProperty.ShadowColor) : color;
		var x = offsetX == null ? style.get(StyleProperty.ShadowOffsetX) : offsetX;
		var y = offsetY == null ? style.get(StyleProperty.ShadowOffsetY) : offsetY;
		var radius = blurRadius == null ? style.get(StyleProperty.ShadowBlur) : blurRadius;
		if (shadowColor == null || shadowColor.alpha <= 0.0)
			return;
		var topLeft = style.get(StyleProperty.RadiusTopLeft);
		var topRight = style.get(StyleProperty.RadiusTopRight);
		var bottomRight = style.get(StyleProperty.RadiusBottomRight);
		var bottomLeft = style.get(StyleProperty.RadiusBottomLeft);
		var resolvedSpread = spread == null ? style.get(StyleProperty.ShadowSpread) : spread;
		canvas.drawBoxShadow(new Rect(0.0, 0.0, geometry.width, geometry.height), x, y, radius,
			resolvedSpread, [topLeft, topRight, bottomRight, bottomLeft], shadowColor);
	}

	override public function copy():Decoration
		return new ShadowDecoration(color, offsetX, offsetY, blurRadius, spread);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		var value:ShadowDecoration = cast other;
		return Effect.equalColor(color, value.color) && offsetX == value.offsetX &&
			offsetY == value.offsetY && blurRadius == value.blurRadius && spread == value.spread;
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration {
		if (other == null || other.kind != kind)
			return Decoration.discrete(this, other, amount);
		var value:ShadowDecoration = cast other;
		if (color == null || value.color == null || offsetX == null || value.offsetX == null ||
			offsetY == null || value.offsetY == null || blurRadius == null ||
			value.blurRadius == null || spread == null || value.spread == null)
			return amount < 0.5 ? copy() : value.copy();
		return new ShadowDecoration(Effect.interpolateColor(color, value.color, amount),
			offsetX + (value.offsetX - offsetX) * amount,
			offsetY + (value.offsetY - offsetY) * amount,
			blurRadius + (value.blurRadius - blurRadius) * amount,
			spread + (value.spread - spread) * amount);
	}

	override public function describe():String
		return 'shadow(${color == null ? "style" : color.red + "," + color.green + "," +
			color.blue + "," + color.alpha},${offsetX == null ? "style" : offsetX},' +
			'${offsetY == null ? "style" : offsetY},${blurRadius == null ? "style" : blurRadius},' +
			'${spread == null ? "style" : spread})';
}
