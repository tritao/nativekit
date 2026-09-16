package nativekit.ui.style;

/** Typed effect families understood by the native compositor. */
enum abstract EffectKind(String) from String to String {
	var Blur = "blur";
	var Brightness = "brightness";
	var Contrast = "contrast";
	var Saturate = "saturate";
	var HueRotate = "hue-rotate";
	var ColorMatrix = "color-matrix";
	var DropShadow = "drop-shadow";
}
