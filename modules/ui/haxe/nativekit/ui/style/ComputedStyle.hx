package nativekit.ui.style;

import LayoutStyle;
import LayoutAxis;
import Insets;
import Color;
import Transform2D;

/** One resolved property value and its optional cascade provenance. */
class ComputedProperty<T> {
	public final value:T;
	public final source:Null<StyleSource>;

	public function new(value:T, source:Null<StyleSource>) {
		this.value = value;
		this.source = source;
	}
}

/** Authoritative Haxe-side result of style resolution. */
class ComputedStyle {
	final values:Map<String, Dynamic>;
	final sources:Map<String, StyleSource>;

	public function new() {
		values = new Map();
		sources = new Map();
	}

	public function set<T>(property:StyleProperty<T>, value:T, source:Null<StyleSource>):Void {
		if (property == null)
			throw "Computed styles require a property";
		values.set(property.name, value);
		if (source == null)
			sources.remove(property.name);
		else
			sources.set(property.name, source);
	}

	public function has<T>(property:StyleProperty<T>):Bool
		return property != null && values.exists(property.name);

	public function get<T>(property:StyleProperty<T>):T {
		if (property == null)
			throw "Computed styles require a property";
		var value = values.get(property.name);
		return value == null && !values.exists(property.name) ? property.defaultValue : cast value;
	}

	public function property<T>(property:StyleProperty<T>):ComputedProperty<T>
		return new ComputedProperty(get(property), sources.get(property.name));

	public function source<T>(property:StyleProperty<T>):Null<StyleSource>
		return sources.get(property.name);

	/** Materializes the resolved layout subset without mutating any input style. */
	public function toLayoutStyle(?base:LayoutStyle):LayoutStyle {
		var result = base == null ? new LayoutStyle() : base.copy();
		if (values.exists(StyleProperty.Width.name)) {
			var width:LayoutAxis = cast values.get(StyleProperty.Width.name);
			result.width = width;
		}
		if (values.exists(StyleProperty.Height.name)) {
			var height:LayoutAxis = cast values.get(StyleProperty.Height.name);
			result.height = height;
		}
		if (values.exists(StyleProperty.Direction.name)) result.direction = cast values.get(StyleProperty.Direction.name);
		if (values.exists(StyleProperty.ChildAlignX.name)) result.childAlignX = cast values.get(StyleProperty.ChildAlignX.name);
		if (values.exists(StyleProperty.ChildAlignY.name)) result.childAlignY = cast values.get(StyleProperty.ChildAlignY.name);
		if (values.exists(StyleProperty.Positioning.name)) result.positioning = cast values.get(StyleProperty.Positioning.name);
		if (values.exists(StyleProperty.PositionX.name)) result.positionX = cast values.get(StyleProperty.PositionX.name);
		if (values.exists(StyleProperty.PositionY.name)) result.positionY = cast values.get(StyleProperty.PositionY.name);
		if (values.exists(StyleProperty.ZIndex.name)) result.zIndex = cast values.get(StyleProperty.ZIndex.name);
		if (values.exists(StyleProperty.ClipToParent.name)) result.clipToParent = cast values.get(StyleProperty.ClipToParent.name);
		if (values.exists(StyleProperty.Padding.name)) {
			var padding:Insets = cast values.get(StyleProperty.Padding.name);
			result.padding = padding;
		}
		if (values.exists(StyleProperty.ChildGap.name)) result.childGap = cast values.get(StyleProperty.ChildGap.name);
		if (values.exists(StyleProperty.Background.name)) {
			var background:Color = cast values.get(StyleProperty.Background.name);
			result.background = background;
		}
		if (values.exists(StyleProperty.RadiusTopLeft.name)) result.radiusTopLeft = cast values.get(StyleProperty.RadiusTopLeft.name);
		if (values.exists(StyleProperty.RadiusTopRight.name)) result.radiusTopRight = cast values.get(StyleProperty.RadiusTopRight.name);
		if (values.exists(StyleProperty.RadiusBottomRight.name)) result.radiusBottomRight = cast values.get(StyleProperty.RadiusBottomRight.name);
		if (values.exists(StyleProperty.RadiusBottomLeft.name)) result.radiusBottomLeft = cast values.get(StyleProperty.RadiusBottomLeft.name);
		if (values.exists(StyleProperty.ClipHorizontal.name)) result.clipHorizontal = cast values.get(StyleProperty.ClipHorizontal.name);
		if (values.exists(StyleProperty.ClipVertical.name)) result.clipVertical = cast values.get(StyleProperty.ClipVertical.name);
		if (values.exists(StyleProperty.Visible.name)) result.visible = cast values.get(StyleProperty.Visible.name);
		if (values.exists(StyleProperty.Transform.name)) {
			var transform:Transform2D = cast values.get(StyleProperty.Transform.name);
			result.transform = transform;
		}
		return result;
	}

	public function copy():ComputedStyle {
		var result = new ComputedStyle();
		for (property in StyleProperty.all())
			if (values.exists(property.name))
				result.set(property, values.get(property.name), sources.get(property.name));
		return result;
	}
}
