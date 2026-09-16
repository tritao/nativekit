package nativekit.ui.style;

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
