package nativekit.ui.style;

/** Subtree alpha shadow; distinct from the lightweight ShadowDecoration. */
class DropShadowEffect extends Effect {
	public final offsetX:Float;
	public final offsetY:Float;
	public final sigma:Float;
	public final color:Color;

	public function new(offsetX:Float, offsetY:Float, sigma:Float, color:Color) {
		super(EffectKind.DropShadow);
		this.offsetX = Effect.requireFinite(offsetX, "Drop-shadow X offset must be finite");
		this.offsetY = Effect.requireFinite(offsetY, "Drop-shadow Y offset must be finite");
		this.sigma = Effect.requireNonNegative(sigma,
			"Drop-shadow sigma must be finite and non-negative");
		if (color == null)
			throw "Drop shadows require a color";
		this.color = Effect.copyColor(color);
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.DropShadow)
			return false;
		var value:DropShadowEffect = cast other;
		return value != null && offsetX == value.offsetX && offsetY == value.offsetY &&
			sigma == value.sigma && Effect.equalColor(color, value.color);
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.DropShadow)
			return Effect.discrete(this, other, amount);
		var value:DropShadowEffect = cast other;
		return new DropShadowEffect(Effect.interpolateFloat(offsetX, value.offsetX, amount),
			Effect.interpolateFloat(offsetY, value.offsetY, amount),
			Effect.interpolateFloat(sigma, value.sigma, amount),
			Effect.interpolateColor(color, value.color, amount));
	}

	override public function copy():Effect
		return new DropShadowEffect(offsetX, offsetY, sigma, color);

	override public function inkOverflow():InkOverflow {
		var spread = sigma * 3.0;
		return new InkOverflow(Math.max(0.0, spread - offsetX),
			Math.max(0.0, spread - offsetY), Math.max(0.0, spread + offsetX),
			Math.max(0.0, spread + offsetY));
	}

	override public function describe():String
		return 'drop-shadow($offsetX,$offsetY,$sigma,' + color.red + ',' + color.green + ',' +
			color.blue + ',' + color.alpha + ')';
}
