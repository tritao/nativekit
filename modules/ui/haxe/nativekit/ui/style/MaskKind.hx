package nativekit.ui.style;

/** Typed source-alpha mask families understood by the native compositor. */
enum abstract MaskKind(Int) from Int to Int {
	var Rectangle = 1;
	var RoundedRect = 2;
	var Circle = 3;
	var LinearGradient = 4;
	var Image = 5;
}
