package nativekit.ui.style;

/** Component shape used by a renderer-owned custom effect parameter. */
enum abstract EffectParameterType(String) from String to String {
	var Float = "float";
	var Vec2 = "vec2";
	var Vec4 = "vec4";
	var Color = "color";
}
