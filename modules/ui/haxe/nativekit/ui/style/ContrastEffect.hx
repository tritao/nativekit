package nativekit.ui.style;

/** Multiplicative RGB contrast adjustment. */
class ContrastEffect extends Effect {
	public final factor:Float;

	public function new(factor:Float) {
		super(EffectKind.Contrast);
		this.factor = Effect.requireNonNegative(factor,
			"Contrast factor must be finite and non-negative");
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.Contrast)
			return false;
		var value:ContrastEffect = cast other;
		return value != null && factor == value.factor;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.Contrast)
			return Effect.discrete(this, other, amount);
		var value:ContrastEffect = cast other;
		return new ContrastEffect(Effect.interpolateFloat(factor, value.factor, amount));
	}

	override public function copy():Effect
		return new ContrastEffect(factor);

	override public function describe():String
		return 'contrast($factor)';
}
