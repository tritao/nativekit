package nativekit.ui.style;

/** Typed value for a native renderer-owned custom shader effect. */
class CustomEffect extends Effect {
	public final definition:CustomEffectDefinition;
	public final parameters:Array<EffectParameter>;
	/** Flattened values written into the backend-neutral layer descriptor. */
	public final components:Array<Float>;

	public function new(definition:CustomEffectDefinition, parameters:Array<EffectParameter>) {
		super(EffectKind.Custom);
		if (definition == null)
			throw "Custom effects require a definition";
		if (parameters == null || parameters.length != definition.parameterTypes.length)
			throw "Custom effect parameter count does not match its definition";
		this.definition = definition;
		this.parameters = [];
		this.components = [];
		for (index in 0...parameters.length) {
			var parameter = parameters[index];
			if (parameter == null || parameter.type != definition.parameterTypes[index])
				throw "Custom effect parameter type does not match its definition";
			this.parameters.push(parameter.copy());
			for (value in parameter.values)
				this.components.push(value);
		}
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.Custom)
			return false;
		var value:CustomEffect = cast other;
		if (value == null || !definition.isEqual(value.definition) ||
			parameters.length != value.parameters.length)
			return false;
		for (index in 0...parameters.length)
			if (!parameters[index].isEqual(value.parameters[index]))
				return false;
		return true;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.Custom)
			return Effect.discrete(this, other, amount);
		var value:CustomEffect = cast other;
		if (value == null || !definition.isEqual(value.definition) ||
			parameters.length != value.parameters.length)
			return amount < 0.5 ? copy() : value == null ? copy() : value.copy();
		var result:Array<EffectParameter> = [];
		for (index in 0...parameters.length)
			result.push(parameters[index].interpolate(value.parameters[index], amount));
		return new CustomEffect(definition, result);
	}

	override public function copy():Effect
		return new CustomEffect(definition, parameters);

	override public function inkOverflow():InkOverflow
		return definition.overflow;

	override public function describe():String {
		var values:Array<String> = [];
		for (parameter in parameters)
			values.push(parameter.describe());
		return 'custom(${definition.describe()}: ' + values.join(",") + ')';
	}
}
