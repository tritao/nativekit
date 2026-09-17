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
	var values:Map<String, Dynamic>;
	var sources:Map<String, StyleSource>;
	public var matchingRules(default, null):Array<StyleSource>;
	var shared:Bool;

	public function new() {
		values = new Map();
		sources = new Map();
		matchingRules = [];
		shared = false;
	}

	public function set<T>(property:StyleProperty<T>, value:T, source:Null<StyleSource>):Void {
		if (property == null)
			throw "Computed styles require a property";
		ensureWritable();
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
		if (value == null && !values.exists(property.name))
			return property.defaultValue;
		return shared || property.name == "effects" || property.name == "backdropEffects" ||
			property.name == "decorations" || property.name == "mask"
			? cast copyValue(cast property, value) : cast value;
	}

	public function property<T>(property:StyleProperty<T>):ComputedProperty<T>
		return new ComputedProperty(get(property), sources.get(property.name));

	public function source<T>(property:StyleProperty<T>):Null<StyleSource>
		return sources.get(property.name);

	/** Records every matching rule, including rules whose declarations were overridden. */
	public function recordMatch(source:StyleSource):Void {
		if (source != null) {
			ensureWritable();
			matchingRules.push(source);
		}
	}

	/** Returns a stable copy for inspectors and external tooling. */
	public function matchingStyleRules():Array<StyleSource>
		return matchingRules.copy();

	public function entries():Array<StyleInspectionEntry> {
		var result:Array<StyleInspectionEntry> = [];
		for (property in StyleProperty.all())
			if (values.exists(property.name))
				result.push(new StyleInspectionEntry(property.name, copyValue(property, values.get(property.name)),
					sources.get(property.name)));
		return result;
	}

	/** Materializes the resolved layout subset without mutating any input style. */
	public function toLayoutStyle(?base:LayoutStyle):LayoutStyle {
		var result = base == null ? new LayoutStyle() : base.copy();
		if (values.exists(StyleProperty.Width.name)) {
			var width:LayoutAxis = cast copyValue(cast StyleProperty.Width, values.get(StyleProperty.Width.name));
			result.width = width;
		}
		if (values.exists(StyleProperty.Height.name)) {
			var height:LayoutAxis = cast copyValue(cast StyleProperty.Height, values.get(StyleProperty.Height.name));
			result.height = height;
		}
		if (values.exists(StyleProperty.Direction.name)) result.direction = cast values.get(StyleProperty.Direction.name);
		if (values.exists(StyleProperty.ChildAlignX.name)) result.childAlignX = cast values.get(StyleProperty.ChildAlignX.name);
		if (values.exists(StyleProperty.ChildAlignY.name)) result.childAlignY = cast values.get(StyleProperty.ChildAlignY.name);
		if (values.exists(StyleProperty.ChildDistribution.name)) result.childDistribution = cast values.get(StyleProperty.ChildDistribution.name);
		if (values.exists(StyleProperty.Positioning.name)) result.positioning = cast values.get(StyleProperty.Positioning.name);
		if (values.exists(StyleProperty.AspectRatio.name)) result.aspectRatio = cast values.get(StyleProperty.AspectRatio.name);
		if (values.exists(StyleProperty.WrapMode.name)) result.wrapMode = cast values.get(StyleProperty.WrapMode.name);
		if (values.exists(StyleProperty.RowGap.name)) result.rowGap = cast values.get(StyleProperty.RowGap.name);
		if (values.exists(StyleProperty.ColumnGap.name)) result.columnGap = cast values.get(StyleProperty.ColumnGap.name);
		if (values.exists(StyleProperty.AlignSelf.name)) result.alignSelf = cast values.get(StyleProperty.AlignSelf.name);
		if (values.exists(StyleProperty.PositionX.name)) result.positionX = cast values.get(StyleProperty.PositionX.name);
		if (values.exists(StyleProperty.PositionY.name)) result.positionY = cast values.get(StyleProperty.PositionY.name);
		if (values.exists(StyleProperty.ZIndex.name)) result.zIndex = cast values.get(StyleProperty.ZIndex.name);
		if (values.exists(StyleProperty.ClipToParent.name)) result.clipToParent = cast values.get(StyleProperty.ClipToParent.name);
		if (values.exists(StyleProperty.Padding.name)) {
			var padding:Insets = cast copyValue(cast StyleProperty.Padding, values.get(StyleProperty.Padding.name));
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
			var transform:Transform2D = cast copyValue(cast StyleProperty.Transform, values.get(StyleProperty.Transform.name));
			result.transform = transform;
		}
		return result;
	}

	public function copy():ComputedStyle {
		var result = new ComputedStyle();
		for (property in StyleProperty.all())
			if (values.exists(property.name))
				result.set(property, copyValue(property, values.get(property.name)), sources.get(property.name));
		for (source in matchingRules)
			result.recordMatch(source);
		return result;
	}

	/** Fast copy used by style caches; map storage is detached only on mutation. */
	public function fork():ComputedStyle {
		var result = new ComputedStyle();
		result.values = values;
		result.sources = sources;
		result.matchingRules = matchingRules.copy();
		shared = true;
		result.shared = true;
		return result;
	}

	function ensureWritable():Void {
		if (!shared)
			return;
		var nextValues:Map<String, Dynamic> = new Map();
		for (key in values.keys())
			nextValues.set(key, values.get(key));
		var nextSources:Map<String, StyleSource> = new Map();
		for (key in sources.keys())
			nextSources.set(key, sources.get(key));
		values = nextValues;
		sources = nextSources;
		shared = false;
	}

	/** Copies the mutable value objects that can be exposed through a computed style. */
	static function copyValue(property:StyleProperty<Dynamic>, value:Dynamic):Dynamic {
		if (value == null)
			return null;
		return switch property.name {
			case "width" | "height":
				var axis:LayoutAxis = cast value;
				new LayoutAxis(axis.sizing, axis.value, axis.min, axis.max, axis.growWeight);
			case "padding":
				var insets:Insets = cast value;
				new Insets(insets.left, insets.top, insets.right, insets.bottom);
			case "transform":
				var transform:Transform2D = cast value;
				new Transform2D(transform.a, transform.b, transform.c, transform.d,
					transform.tx, transform.ty);
			case "effects" | "backdropEffects":
				var effects:EffectChain = cast value;
				effects == null ? null : effects.copy();
			case "decorations":
				var decorations:DecorationChain = cast value;
				decorations == null ? null : decorations.copy();
			case "mask":
				var mask:Mask = cast value;
				mask == null ? null : mask.copy();
			default:
				value;
		};
	}
}
