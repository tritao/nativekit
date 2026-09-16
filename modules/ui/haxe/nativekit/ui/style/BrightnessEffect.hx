package nativekit.ui.style;

/** Multiplicative RGB brightness adjustment. */
class BrightnessEffect extends Effect {
	public final factor:Float;

	public function new(factor:Float) {
		super(EffectKind.Brightness);
		this.factor = Effect.requireNonNegative(factor,
			"Brightness factor must be finite and non-negative");
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.Brightness)
			return false;
		var value:BrightnessEffect = cast other;
		return value != null && factor == value.factor;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.Brightness)
			return Effect.discrete(this, other, amount);
		var value:BrightnessEffect = cast other;
		return new BrightnessEffect(Effect.interpolateFloat(factor, value.factor, amount));
	}

	override public function copy():Effect
		return new BrightnessEffect(factor);

	override public function describe():String
		return 'brightness($factor)';
}
