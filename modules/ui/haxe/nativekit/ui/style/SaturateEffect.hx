package nativekit.ui.style;

/** Multiplicative color saturation adjustment. */
class SaturateEffect extends Effect {
	public final factor:Float;

	public function new(factor:Float) {
		super(EffectKind.Saturate);
		this.factor = Effect.requireNonNegative(factor,
			"Saturation factor must be finite and non-negative");
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.Saturate)
			return false;
		var value:SaturateEffect = cast other;
		return value != null && factor == value.factor;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.Saturate)
			return Effect.discrete(this, other, amount);
		var value:SaturateEffect = cast other;
		return new SaturateEffect(Effect.interpolateFloat(factor, value.factor, amount));
	}

	override public function copy():Effect
		return new SaturateEffect(factor);

	override public function describe():String
		return 'saturate($factor)';
}
