package nativekit.ui.style;

import nativekit.ui.style.ColorMatrixEffect;
import nativekit.ui.style.BrightnessEffect;
import nativekit.ui.style.ContrastEffect;
import nativekit.ui.style.SaturateEffect;
import nativekit.ui.style.HueRotateEffect;

/** Ordered, value-semantic collection of post-layout visual effects. */
class EffectChain {
	public final effects:Array<Effect>;

	public function new(effects:Array<Effect> = null) {
		this.effects = [];
		if (effects != null)
			for (effect in effects) {
				if (effect == null)
					throw "Effect chains cannot contain null effects";
				this.effects.push(effect.copy());
			}
	}

	public static function empty():EffectChain
		return new EffectChain();

	public static function of(effects:Array<Effect>):EffectChain
		return new EffectChain(effects);

	public function copy():EffectChain
		return new EffectChain(effects);

	public function isEqual(other:EffectChain):Bool {
		if (other == null || effects.length != other.effects.length)
			return false;
		for (index in 0...effects.length)
			if (!effects[index].isEqual(other.effects[index]))
				return false;
		return true;
	}

	/**
	 * Interpolates chains with the same effect shape. Chains that change shape
	 * switch at the midpoint, since a value-only chain cannot cross-fade a
	 * variable number of compositor passes.
	 */
	public static function interpolate(left:EffectChain, right:EffectChain,
			amount:Float):EffectChain {
		if (left == null)
			return right == null ? empty() : right.copy();
		if (right == null)
			return left.copy();
		if (left.effects.length != right.effects.length)
			return amount < 0.5 ? left.copy() : right.copy();
		var result:Array<Effect> = [];
		for (index in 0...left.effects.length) {
			var from = left.effects[index];
			var to = right.effects[index];
			if (from.kind != to.kind)
				return amount < 0.5 ? left.copy() : right.copy();
			result.push(from.interpolate(to, amount));
		}
		return new EffectChain(result);
	}

	/** Returns the total extent needed when the chain is applied in order. */
	public function inkOverflow():InkOverflow {
		var result = InkOverflow.zero();
		for (effect in effects)
			result = result.added(effect.inkOverflow());
		return result;
	}

	/**
	 * Collapses color-adjustment effects into one row-major 4x5 matrix.
	 * Geometry-changing effects are intentionally rejected by this PR; their
	 * descriptors will be added when the corresponding native passes land.
	 */
	public function colorMatrix():Array<Float> {
		var result = identityMatrix();
		for (effect in effects) {
			var next:Array<Float> = switch effect.kind {
				case EffectKind.Brightness:
					var value:BrightnessEffect = cast effect;
					brightnessMatrix(value.factor);
				case EffectKind.Contrast:
					var value:ContrastEffect = cast effect;
					contrastMatrix(value.factor);
				case EffectKind.Saturate:
					var value:SaturateEffect = cast effect;
					saturateMatrix(value.factor);
				case EffectKind.HueRotate:
					var value:HueRotateEffect = cast effect;
					hueRotateMatrix(value.degrees);
				case EffectKind.ColorMatrix:
					var value:ColorMatrixEffect = cast effect;
					value.matrix.copy();
				case EffectKind.Blur | EffectKind.DropShadow:
					throw "This effect is not supported by the color-matrix pass";
				default:
					throw "Unknown effect kind";
			};
			result = multiply(next, result);
		}
		return result;
	}

	static function identityMatrix():Array<Float> {
		var result:Array<Float> = [];
		for (row in 0...4)
			for (column in 0...5)
				result.push(row == column ? 1.0 : 0.0);
		return result;
	}

	static function brightnessMatrix(factor:Float):Array<Float>
		return [factor, 0.0, 0.0, 0.0, 0.0,
			0.0, factor, 0.0, 0.0, 0.0,
			0.0, 0.0, factor, 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0, 0.0];

	static function contrastMatrix(factor:Float):Array<Float> {
		var bias = 0.5 * (1.0 - factor);
		return [factor, 0.0, 0.0, 0.0, bias,
			0.0, factor, 0.0, 0.0, bias,
			0.0, 0.0, factor, 0.0, bias,
			0.0, 0.0, 0.0, 1.0, 0.0];
	}

	static function saturateMatrix(factor:Float):Array<Float> {
		var red = 0.2126;
		var green = 0.7152;
		var blue = 0.0722;
		var inverse = 1.0 - factor;
		return [red * inverse + factor, green * inverse, blue * inverse, 0.0, 0.0,
			red * inverse, green * inverse + factor, blue * inverse, 0.0, 0.0,
			red * inverse, green * inverse, blue * inverse + factor, 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0, 0.0];
	}

	static function hueRotateMatrix(degrees:Float):Array<Float> {
		var radians = degrees * Math.PI / 180.0;
		var cosine = Math.cos(radians);
		var sine = Math.sin(radians);
		return [0.213 + cosine * 0.787 - sine * 0.213,
			0.715 - cosine * 0.715 - sine * 0.715,
			0.072 - cosine * 0.072 + sine * 0.928, 0.0, 0.0,
			0.213 - cosine * 0.213 + sine * 0.143,
			0.715 + cosine * 0.285 + sine * 0.140,
			0.072 - cosine * 0.072 - sine * 0.283, 0.0, 0.0,
			0.213 - cosine * 0.213 - sine * 0.787,
			0.715 - cosine * 0.715 + sine * 0.715,
			0.072 + cosine * 0.928 + sine * 0.072, 0.0, 0.0,
			0.0, 0.0, 0.0, 1.0, 0.0];
	}

	/** Returns left * right for row-major 4x5 color matrices. */
	static function multiply(left:Array<Float>, right:Array<Float>):Array<Float> {
		var result:Array<Float> = [];
		for (row in 0...4) {
			for (column in 0...5) {
				var value = column == 4 ? left[row * 5 + 4] : 0.0;
				for (inner in 0...4)
					value += left[row * 5 + inner] * right[inner * 5 + column];
				result.push(value);
			}
		}
		return result;
	}

	/** Stable cache/inspection key for a resolved chain. */
	public function key():String {
		var values:Array<String> = [];
		for (effect in effects)
			values.push(effect.describe());
		return values.join("|");
	}

	public function toString():String
		return "[" + key() + "]";
}
