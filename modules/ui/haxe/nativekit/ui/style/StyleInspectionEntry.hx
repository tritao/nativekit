package nativekit.ui.style;

import Color;
import Insets;
import LayoutAxis;

/** Inspectable computed property entry kept as a standalone Haxe module. */
class StyleInspectionEntry {
	public final name:String;
	public final value:Dynamic;
	public final source:Null<StyleSource>;

	public function new(name:String, value:Dynamic, source:Null<StyleSource>) {
		this.name = name;
		this.value = value;
		this.source = source;
	}

	/** Formats common typed values for headless logs and the interactive inspector. */
	public function describe():String {
		return switch name {
			case "background" | "borderColor" | "outlineColor" | "shadowColor" | "textColor" |
				"progressTrackColor" | "progressFillColor" | "sliderTrackColor" |
				"sliderFillColor" | "sliderThumbColor":
				var color:Color = cast value;
				color == null ? "null" : 'rgba(${color.red}, ${color.green}, ${color.blue}, ${color.alpha})';
			case "padding":
				var insets:Insets = cast value;
				insets == null ? "null" :
					'${insets.left}, ${insets.top}, ${insets.right}, ${insets.bottom}';
			case "width" | "height":
				var axis:LayoutAxis = cast value;
				axis == null ? "null" : Std.string(axis.sizing) + "(" + Std.string(axis.value) + ")";
			case "effects" | "backdropEffects":
				var effects:EffectChain = cast value;
				effects == null ? "null" : effects.toString();
			case "mask":
				var mask:Mask = cast value;
				mask == null ? "null" : mask.toString();
			default:
				Std.string(value);
		};
	}
}
