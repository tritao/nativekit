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
import LayoutStyle;
import LayoutWrapMode;
import Transform2D;

/** Metadata and layout bridge for one typed style property. */
class StyleProperty<T> {
	public final name:String;
	public final defaultValue:T;
	public final inherited:Bool;
	public final impact:StyleImpact;
	public final interpolate:Null<T->T->Float->T>;
	final readLayout:Null<LayoutStyle->T>;
	final writeLayout:Null<LayoutStyle->T->Void>;
	final equalValue:Null<T->T->Bool>;

	public function new(name:String, defaultValue:T, inherited:Bool, impact:StyleImpact,
			?interpolate:T->T->Float->T, ?readLayout:LayoutStyle->T,
			?writeLayout:LayoutStyle->T->Void, ?equalValue:T->T->Bool) {
		if (name == null || name.length == 0)
			throw "Style properties require a name";
		this.name = name;
		this.defaultValue = defaultValue;
		this.inherited = inherited;
		this.impact = impact;
		this.interpolate = interpolate;
		this.readLayout = readLayout;
		this.writeLayout = writeLayout;
		this.equalValue = equalValue;
	}

	public function read(style:LayoutStyle):T {
		if (style == null || readLayout == null)
			throw 'Style property ${name} is not backed by LayoutStyle';
		return readLayout(style);
	}

	public function write(style:LayoutStyle, value:T):Void {
		if (style != null && writeLayout != null)
			writeLayout(style, value);
	}

	public function isEqual(left:T, right:T):Bool
		return equalValue == null ? left == right : equalValue(left, right);

	public function hasLayoutBinding():Bool
		return readLayout != null && writeLayout != null;

	static function colorEqual(left:Color, right:Color):Bool
		return left == right || (left != null && right != null &&
			left.red == right.red && left.green == right.green &&
			left.blue == right.blue && left.alpha == right.alpha);

	static function insetsEqual(left:Insets, right:Insets):Bool
		return left == right || (left != null && right != null &&
			left.left == right.left && left.top == right.top &&
			left.right == right.right && left.bottom == right.bottom);

	static function axisEqual(left:LayoutAxis, right:LayoutAxis):Bool
		return left == right || (left != null && right != null &&
			left.sizing == right.sizing && left.value == right.value);

	static function transformEqual(left:Transform2D, right:Transform2D):Bool
		return left == right || (left != null && right != null &&
			left.a == right.a && left.b == right.b && left.c == right.c &&
			left.d == right.d && left.tx == right.tx && left.ty == right.ty);

	static function floatInterpolate(left:Float, right:Float, amount:Float):Float
		return left + (right - left) * amount;

	static function colorInterpolate(left:Color, right:Color, amount:Float):Color
		return Color.rgba(left.red + (right.red - left.red) * amount,
			left.green + (right.green - left.green) * amount,
			left.blue + (right.blue - left.blue) * amount,
			left.alpha + (right.alpha - left.alpha) * amount);

	public static final Width:StyleProperty<LayoutAxis> = new StyleProperty(
		"width", LayoutAxis.fit(), false, StyleImpact.Layout, null,
		function(style) return style.width, function(style, value) style.width = value, axisEqual);
	public static final Height:StyleProperty<LayoutAxis> = new StyleProperty(
		"height", LayoutAxis.fit(), false, StyleImpact.Layout, null,
		function(style) return style.height, function(style, value) style.height = value, axisEqual);
	public static final Direction:StyleProperty<LayoutDirection> = new StyleProperty(
		"direction", LayoutDirection.TopToBottom, false, StyleImpact.Layout, null,
		function(style) return style.direction, function(style, value) style.direction = value);
	public static final ChildAlignX:StyleProperty<LayoutAlignmentX> = new StyleProperty(
		"childAlignX", LayoutAlignmentX.Start, false, StyleImpact.Layout, null,
		function(style) return style.childAlignX, function(style, value) style.childAlignX = value);
	public static final ChildAlignY:StyleProperty<LayoutAlignmentY> = new StyleProperty(
		"childAlignY", LayoutAlignmentY.Start, false, StyleImpact.Layout, null,
		function(style) return style.childAlignY, function(style, value) style.childAlignY = value);
	public static final ChildDistribution:StyleProperty<LayoutDistribution> = new StyleProperty(
		"childDistribution", LayoutDistribution.Start, false, StyleImpact.Layout, null,
		function(style) return style.childDistribution, function(style, value) style.childDistribution = value);
	public static final Positioning:StyleProperty<LayoutPositioning> = new StyleProperty(
		"positioning", LayoutPositioning.Flow, false, StyleImpact.Layout, null,
		function(style) return style.positioning, function(style, value) style.positioning = value);
	public static final AspectRatio:StyleProperty<Float> = new StyleProperty(
		"aspectRatio", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.aspectRatio, function(style, value) style.aspectRatio = value);
	public static final WrapMode:StyleProperty<LayoutWrapMode> = new StyleProperty(
		"wrapMode", LayoutWrapMode.NoWrap, false, StyleImpact.Layout, null,
		function(style) return style.wrapMode, function(style, value) style.wrapMode = value);
	public static final RowGap:StyleProperty<Float> = new StyleProperty(
		"rowGap", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.rowGap, function(style, value) style.rowGap = value);
	public static final ColumnGap:StyleProperty<Float> = new StyleProperty(
		"columnGap", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.columnGap, function(style, value) style.columnGap = value);
	public static final AlignSelf:StyleProperty<LayoutSelfAlignment> = new StyleProperty(
		"alignSelf", LayoutSelfAlignment.Inherit, false, StyleImpact.Layout, null,
		function(style) return style.alignSelf, function(style, value) style.alignSelf = value);
	public static final PositionX:StyleProperty<Float> = new StyleProperty(
		"positionX", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.positionX, function(style, value) style.positionX = value);
	public static final PositionY:StyleProperty<Float> = new StyleProperty(
		"positionY", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.positionY, function(style, value) style.positionY = value);
	public static final ZIndex:StyleProperty<Int> = new StyleProperty(
		"zIndex", 0, false, StyleImpact.Paint, null,
		function(style) return style.zIndex, function(style, value) style.zIndex = value);
	public static final ClipToParent:StyleProperty<Bool> = new StyleProperty(
		"clipToParent", true, false, StyleImpact.Paint, null,
		function(style) return style.clipToParent, function(style, value) style.clipToParent = value);
	public static final Padding:StyleProperty<Insets> = new StyleProperty(
		"padding", new Insets(0.0, 0.0, 0.0, 0.0), false, StyleImpact.Layout, null,
		function(style) return style.padding, function(style, value) style.padding = value, insetsEqual);
	public static final ChildGap:StyleProperty<Float> = new StyleProperty(
		"childGap", 0.0, false, StyleImpact.Layout, null,
		function(style) return style.childGap, function(style, value) style.childGap = value);
	public static final Background:StyleProperty<Color> = new StyleProperty(
		"background", Color.rgba(0.0, 0.0, 0.0, 0.0), false, StyleImpact.Paint, colorInterpolate,
		function(style) return style.background, function(style, value) style.background = value, colorEqual);
	public static final RadiusTopLeft:StyleProperty<Float> = new StyleProperty(
		"radiusTopLeft", 0.0, false, StyleImpact.Paint, floatInterpolate,
		function(style) return style.radiusTopLeft, function(style, value) style.radiusTopLeft = value);
	public static final RadiusTopRight:StyleProperty<Float> = new StyleProperty(
		"radiusTopRight", 0.0, false, StyleImpact.Paint, floatInterpolate,
		function(style) return style.radiusTopRight, function(style, value) style.radiusTopRight = value);
	public static final RadiusBottomRight:StyleProperty<Float> = new StyleProperty(
		"radiusBottomRight", 0.0, false, StyleImpact.Paint, floatInterpolate,
		function(style) return style.radiusBottomRight, function(style, value) style.radiusBottomRight = value);
	public static final RadiusBottomLeft:StyleProperty<Float> = new StyleProperty(
		"radiusBottomLeft", 0.0, false, StyleImpact.Paint, floatInterpolate,
		function(style) return style.radiusBottomLeft, function(style, value) style.radiusBottomLeft = value);
	public static final ClipHorizontal:StyleProperty<Bool> = new StyleProperty(
		"clipHorizontal", false, false, StyleImpact.Paint, null,
		function(style) return style.clipHorizontal, function(style, value) style.clipHorizontal = value);
	public static final ClipVertical:StyleProperty<Bool> = new StyleProperty(
		"clipVertical", false, false, StyleImpact.Paint, null,
		function(style) return style.clipVertical, function(style, value) style.clipVertical = value);
	public static final Visible:StyleProperty<Bool> = new StyleProperty(
		"visible", true, false, StyleImpact.Paint, null,
		function(style) return style.visible, function(style, value) style.visible = value);
	public static final Transform:StyleProperty<Transform2D> = new StyleProperty(
		"transform", Transform2D.identity(), false, StyleImpact.Composite, null,
		function(style) return style.transform, function(style, value) style.transform = value, transformEqual);
	public static final BorderColor:StyleProperty<Color> = new StyleProperty(
		"borderColor", Color.rgba(0.0, 0.0, 0.0, 0.0), false, StyleImpact.Paint, colorInterpolate,
		null, null, colorEqual);
	public static final BorderWidth:StyleProperty<Float> = new StyleProperty(
		"borderWidth", 0.0, false, StyleImpact.Paint, floatInterpolate);
	public static final OutlineColor:StyleProperty<Color> = new StyleProperty(
		"outlineColor", Color.rgba(0.0, 0.0, 0.0, 0.0), false, StyleImpact.Paint, colorInterpolate,
		null, null, colorEqual);
	public static final OutlineWidth:StyleProperty<Float> = new StyleProperty(
		"outlineWidth", 0.0, false, StyleImpact.Paint, floatInterpolate);
	public static final ShadowColor:StyleProperty<Color> = new StyleProperty(
		"shadowColor", Color.rgba(0.0, 0.0, 0.0, 0.0), false, StyleImpact.Paint, colorInterpolate,
		null, null, colorEqual);
	public static final ShadowOffsetX:StyleProperty<Float> = new StyleProperty(
		"shadowOffsetX", 0.0, false, StyleImpact.Paint, floatInterpolate);
	public static final ShadowOffsetY:StyleProperty<Float> = new StyleProperty(
		"shadowOffsetY", 0.0, false, StyleImpact.Paint, floatInterpolate);
	public static final ShadowBlur:StyleProperty<Float> = new StyleProperty(
		"shadowBlur", 0.0, false, StyleImpact.Paint, floatInterpolate);
	public static final Opacity:StyleProperty<Float> = new StyleProperty(
		"opacity", 1.0, false, StyleImpact.Composite, floatInterpolate);
	/** Paint inputs used by the retained custom progress renderer. */
	public static final ProgressTrackColor:StyleProperty<Color> = new StyleProperty(
		"progressTrackColor", Color.rgba(0.19, 0.21, 0.25, 1.0), false,
		StyleImpact.Paint, colorInterpolate, null, null, colorEqual);
	public static final ProgressFillColor:StyleProperty<Color> = new StyleProperty(
		"progressFillColor", Color.rgba(0.22, 0.52, 0.84, 1.0), false,
		StyleImpact.Paint, colorInterpolate, null, null, colorEqual);
	/** Paint inputs used by the retained custom slider renderer. */
	public static final SliderTrackColor:StyleProperty<Color> = new StyleProperty(
		"sliderTrackColor", Color.rgba(0.16, 0.18, 0.22, 1.0), false,
		StyleImpact.Paint, colorInterpolate, null, null, colorEqual);
	public static final SliderFillColor:StyleProperty<Color> = new StyleProperty(
		"sliderFillColor", Color.rgba(0.22, 0.48, 0.86, 1.0), false,
		StyleImpact.Paint, colorInterpolate, null, null, colorEqual);
	public static final SliderThumbColor:StyleProperty<Color> = new StyleProperty(
		"sliderThumbColor", Color.rgba(0.96, 0.97, 0.99, 1.0), false,
		StyleImpact.Paint, colorInterpolate, null, null, colorEqual);

	/** Inherited typography/paint inputs are computed here and applied by text widgets. */
	public static final TextColor:StyleProperty<Color> = new StyleProperty(
		"textColor", Color.rgba(1.0, 1.0, 1.0, 1.0), true, StyleImpact.Paint, null, null, null, colorEqual);
	public static final FontSize:StyleProperty<Float> = new StyleProperty(
		"fontSize", 16.0, true, StyleImpact.Layout | StyleImpact.TextLayout | StyleImpact.Paint);
	public static final LetterSpacing:StyleProperty<Float> = new StyleProperty(
		"letterSpacing", 0.0, true, StyleImpact.TextLayout | StyleImpact.Paint);

	static var definitions:Array<StyleProperty<Dynamic>>;

	static function dynamicProperty<T>(property:StyleProperty<T>):StyleProperty<Dynamic>
		return cast(property, StyleProperty<Dynamic>);

	/** Stable registry used by default construction, local-style projection, and inspection. */
	public static function all():Array<StyleProperty<Dynamic>> {
		if (definitions == null)
			definitions = [
				dynamicProperty(Width), dynamicProperty(Height), dynamicProperty(Direction),
				dynamicProperty(ChildAlignX), dynamicProperty(ChildAlignY), dynamicProperty(ChildDistribution),
				dynamicProperty(Positioning), dynamicProperty(AspectRatio), dynamicProperty(WrapMode),
				dynamicProperty(RowGap), dynamicProperty(ColumnGap), dynamicProperty(AlignSelf),
				dynamicProperty(PositionX), dynamicProperty(PositionY), dynamicProperty(ZIndex),
				dynamicProperty(ClipToParent), dynamicProperty(Padding), dynamicProperty(ChildGap),
				dynamicProperty(Background), dynamicProperty(RadiusTopLeft), dynamicProperty(RadiusTopRight),
				dynamicProperty(RadiusBottomRight), dynamicProperty(RadiusBottomLeft),
				dynamicProperty(ClipHorizontal), dynamicProperty(ClipVertical), dynamicProperty(Visible),
				dynamicProperty(Transform), dynamicProperty(TextColor), dynamicProperty(FontSize),
				dynamicProperty(LetterSpacing), dynamicProperty(BorderColor), dynamicProperty(BorderWidth),
				dynamicProperty(OutlineColor), dynamicProperty(OutlineWidth), dynamicProperty(ShadowColor),
				dynamicProperty(ShadowOffsetX), dynamicProperty(ShadowOffsetY), dynamicProperty(ShadowBlur),
				dynamicProperty(Opacity), dynamicProperty(ProgressTrackColor), dynamicProperty(ProgressFillColor),
				dynamicProperty(SliderTrackColor), dynamicProperty(SliderFillColor),
				dynamicProperty(SliderThumbColor)
			];
		return definitions;
	}
}
