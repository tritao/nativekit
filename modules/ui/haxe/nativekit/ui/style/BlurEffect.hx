package nativekit.ui.style;

/** Gaussian blur described by its standard deviation in logical pixels. */
class BlurEffect extends Effect {
	public final sigma:Float;

	public function new(sigma:Float) {
		super(EffectKind.Blur);
		this.sigma = Effect.requireNonNegative(sigma,
			"Blur sigma must be finite and non-negative");
	}

	public static function withSigma(value:Float):BlurEffect
		return new BlurEffect(value);

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.Blur)
			return false;
		var value:BlurEffect = cast other;
		return value != null && kind == value.kind && sigma == value.sigma;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.Blur)
			return Effect.discrete(this, other, amount);
		var value:BlurEffect = cast other;
		return new BlurEffect(Effect.interpolateFloat(sigma, value.sigma, amount));
	}

	override public function copy():Effect
		return new BlurEffect(sigma);

	override public function inkOverflow():InkOverflow
		return new InkOverflow(sigma * 3.0, sigma * 3.0, sigma * 3.0, sigma * 3.0);

	override public function describe():String
		return 'blur($sigma)';
}
