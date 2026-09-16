package nativekit.ui.style;

import Color;
import Insets;
import LayoutAlignment;
import LayoutAxis;
import LayoutDirection;
import LayoutPositioning;

/** One typed declaration passed to a StyleRule. */
class StyleValue {
	public final property:Dynamic;
	public final value:Dynamic;

	public function new(property:Dynamic, value:Dynamic) {
		if (property == null)
			throw "Style declarations require a property";
		this.property = property;
		this.value = value;
	}

	public static function of<T>(property:StyleProperty<T>, value:T):StyleValue
		return new StyleValue(property, value);

	public static function width(value:LayoutAxis):StyleValue
		return of(StyleProperty.Width, value);

	public static function height(value:LayoutAxis):StyleValue
		return of(StyleProperty.Height, value);

	public static function direction(value:LayoutDirection):StyleValue
		return of(StyleProperty.Direction, value);

	public static function alignX(value:LayoutAlignment):StyleValue
		return of(StyleProperty.ChildAlignX, value);

	public static function alignY(value:LayoutAlignment):StyleValue
		return of(StyleProperty.ChildAlignY, value);

	public static function positioning(value:LayoutPositioning):StyleValue
		return of(StyleProperty.Positioning, value);

	public static function background(value:Color):StyleValue
		return of(StyleProperty.Background, value);

	public static function padding(value:Insets):StyleValue
		return of(StyleProperty.Padding, value);

	public static function paddingSymmetric(horizontal:Float, vertical:Float):StyleValue
		return padding(new Insets(horizontal, vertical, horizontal, vertical));

	public static function radius(property:StyleProperty<Float>, value:Float):StyleValue
		return of(property, value);

	public static function radiusAll(value:Float):Array<StyleValue>
		return [radius(StyleProperty.RadiusTopLeft, value), radius(StyleProperty.RadiusTopRight, value),
			radius(StyleProperty.RadiusBottomRight, value), radius(StyleProperty.RadiusBottomLeft, value)];

	public static function textColor(value:Color):StyleValue
		return of(StyleProperty.TextColor, value);

	public static function fontSize(value:Float):StyleValue
		return of(StyleProperty.FontSize, value);

	public static function letterSpacing(value:Float):StyleValue
		return of(StyleProperty.LetterSpacing, value);

}
