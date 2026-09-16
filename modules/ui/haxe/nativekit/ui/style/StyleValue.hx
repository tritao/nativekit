package nativekit.ui.style;

import Color;
import Insets;
import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDistribution;
import LayoutDirection;
import LayoutPositioning;
import LayoutSelfAlignment;
import LayoutWrapMode;

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

	public static function alignX(value:LayoutAlignmentX):StyleValue
		return of(StyleProperty.ChildAlignX, value);

	public static function alignY(value:LayoutAlignmentY):StyleValue
		return of(StyleProperty.ChildAlignY, value);

	public static function distribution(value:LayoutDistribution):StyleValue
		return of(StyleProperty.ChildDistribution, value);

	public static function positioning(value:LayoutPositioning):StyleValue
		return of(StyleProperty.Positioning, value);

	public static function aspectRatio(value:Float):StyleValue
		return of(StyleProperty.AspectRatio, value);

	public static function wrapMode(value:LayoutWrapMode):StyleValue
		return of(StyleProperty.WrapMode, value);

	public static function rowGap(value:Float):StyleValue
		return of(StyleProperty.RowGap, value);

	public static function columnGap(value:Float):StyleValue
		return of(StyleProperty.ColumnGap, value);

	public static function alignSelf(value:LayoutSelfAlignment):StyleValue
		return of(StyleProperty.AlignSelf, value);

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

	public static function borderColor(value:Color):StyleValue
		return of(StyleProperty.BorderColor, value);

	public static function borderWidth(value:Float):StyleValue
		return of(StyleProperty.BorderWidth, value);

	public static function outlineColor(value:Color):StyleValue
		return of(StyleProperty.OutlineColor, value);

	public static function outlineWidth(value:Float):StyleValue
		return of(StyleProperty.OutlineWidth, value);

	public static function shadowColor(value:Color):StyleValue
		return of(StyleProperty.ShadowColor, value);

	public static function shadowOffset(x:Float, y:Float):Array<StyleValue>
		return [of(StyleProperty.ShadowOffsetX, x), of(StyleProperty.ShadowOffsetY, y)];

	public static function shadowBlur(value:Float):StyleValue
		return of(StyleProperty.ShadowBlur, value);

	public static function opacity(value:Float):StyleValue
		return of(StyleProperty.Opacity, value);

	public static function progressTrackColor(value:Color):StyleValue
		return of(StyleProperty.ProgressTrackColor, value);

	public static function progressFillColor(value:Color):StyleValue
		return of(StyleProperty.ProgressFillColor, value);

	public static function sliderTrackColor(value:Color):StyleValue
		return of(StyleProperty.SliderTrackColor, value);

	public static function sliderFillColor(value:Color):StyleValue
		return of(StyleProperty.SliderFillColor, value);

	public static function sliderThumbColor(value:Color):StyleValue
		return of(StyleProperty.SliderThumbColor, value);

}
