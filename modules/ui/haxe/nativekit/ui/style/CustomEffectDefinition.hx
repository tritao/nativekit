package nativekit.ui.style;

import nativekit.ui.style.EffectParameterTypeUtil;

/**
 * Renderer-owned contract for one custom effect implementation.
 *
 * The ID is stable within the native renderer registration that implements
 * it. Haxe carries only this typed contract; shader source, pipelines, and
 * backend-specific mapping remain native concerns.
 */
class CustomEffectDefinition {
	public final id:Int;
	public final name:String;
	public final parameterTypes:Array<EffectParameterType>;
	public final overflow:InkOverflow;
	public final componentCount:Int;

	public function new(id:Int, name:String, parameterTypes:Array<EffectParameterType>,
			overflow:InkOverflow = null) {
		if (id <= 0)
			throw "Custom effect registrations require a positive ID";
		if (name == null || name.length == 0)
			throw "Custom effect registrations require a name";
		if (parameterTypes == null)
			throw "Custom effect registrations require parameter types";
		var components = 0;
		for (type in parameterTypes) {
			if (type == null || EffectParameterTypeUtil.componentCount(type) <= 0)
				throw "Custom effect registrations contain an invalid parameter type";
			components += EffectParameterTypeUtil.componentCount(type);
		}
		if (components > 20)
			throw "Custom effect registrations support at most 20 float components";
		this.id = id;
		this.name = name;
		this.parameterTypes = parameterTypes.copy();
		this.overflow = overflow == null ? InkOverflow.zero() : overflow;
		for (value in [this.overflow.left, this.overflow.top, this.overflow.right, this.overflow.bottom])
			Effect.requireNonNegative(value, "Custom effect ink overflow must be finite and non-negative");
		this.componentCount = components;
	}

	public function isEqual(other:CustomEffectDefinition):Bool {
		if (other == null || id != other.id || name != other.name ||
			componentCount != other.componentCount ||
			!overflow.isEqual(other.overflow) || parameterTypes.length != other.parameterTypes.length)
			return false;
		for (index in 0...parameterTypes.length)
			if (parameterTypes[index] != other.parameterTypes[index])
				return false;
		return true;
	}

	public function describe():String
		return name + "#" + id;
}
