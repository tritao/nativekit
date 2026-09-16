package nativekit.ui.style;

import Color;

/** One typed value supplied to a custom effect registration. */
class EffectParameter {
	public final type:EffectParameterType;
	public final values:Array<Float>;

	private function new(type:EffectParameterType, values:Array<Float>) {
		if (type == null || values == null || values.length != EffectParameterTypeUtil.componentCount(type))
			throw "Custom effect parameter shape does not match its type";
		this.type = type;
		this.values = [];
		for (value in values)
			this.values.push(Effect.requireFinite(value,
				"Custom effect parameters must be finite"));
	}

	public static function scalar(value:Float):EffectParameter
		return new EffectParameter(EffectParameterType.Float, [value]);

	public static function vec2(x:Float, y:Float):EffectParameter
		return new EffectParameter(EffectParameterType.Vec2, [x, y]);

	public static function vec4(x:Float, y:Float, z:Float, w:Float):EffectParameter
		return new EffectParameter(EffectParameterType.Vec4, [x, y, z, w]);

	public static function color(value:Color):EffectParameter {
		if (value == null)
			throw "Custom color parameters require a color";
		return new EffectParameter(EffectParameterType.Color,
			[value.red, value.green, value.blue, value.alpha]);
	}

	public function copy():EffectParameter
		return new EffectParameter(type, values);

	public function isEqual(other:EffectParameter):Bool {
		if (other == null || type != other.type || values.length != other.values.length)
			return false;
		for (index in 0...values.length)
			if (values[index] != other.values[index])
				return false;
		return true;
	}

	public function interpolate(other:EffectParameter, amount:Float):EffectParameter {
		if (other == null || type != other.type || values.length != other.values.length)
			return amount < 0.5 ? copy() : other == null ? copy() : other.copy();
		var result:Array<Float> = [];
		for (index in 0...values.length)
			result.push(Effect.interpolateFloat(values[index], other.values[index], amount));
		return new EffectParameter(type, result);
	}

	public function describe():String {
		var text:Array<String> = [];
		for (value in values)
			text.push(Std.string(value));
		return Std.string(type) + "(" + text.join(",") + ")";
	}
}
