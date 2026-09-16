package nativekit.ui.style;

/** Hue rotation in degrees. */
class HueRotateEffect extends Effect {
	public final degrees:Float;

	public function new(degrees:Float) {
		super(EffectKind.HueRotate);
		this.degrees = Effect.requireFinite(degrees,
			"Hue rotation must be finite");
	}

	public static function withDegrees(value:Float):HueRotateEffect
		return new HueRotateEffect(value);

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.HueRotate)
			return false;
		var value:HueRotateEffect = cast other;
		return value != null && degrees == value.degrees;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.HueRotate)
			return Effect.discrete(this, other, amount);
		var value:HueRotateEffect = cast other;
		return new HueRotateEffect(Effect.interpolateFloat(degrees, value.degrees, amount));
	}

	override public function copy():Effect
		return new HueRotateEffect(degrees);

	override public function describe():String
		return 'hue-rotate($degrees)';
}
