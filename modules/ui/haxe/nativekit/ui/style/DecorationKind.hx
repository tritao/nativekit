package nativekit.ui.style;

/** Typed decoration families understood by the style and paint systems. */
enum abstract DecorationKind(String) from String to String {
	var Background = "background";
	var Gradient = "gradient";
	var Image = "image";
	var NineSlice = "nine-slice";
	var Border = "border";
	var Outline = "outline";
	var Shadow = "shadow";
	/** A user-defined renderer-owned decoration. */
	var Custom = "custom";
}
