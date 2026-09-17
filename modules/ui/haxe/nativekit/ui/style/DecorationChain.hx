package nativekit.ui.style;

/** Ordered, value-aware collection of post-layout style decorations. */
class DecorationChain {
	public final decorations:Array<Decoration>;

	public function new(decorations:Array<Decoration> = null) {
		this.decorations = [];
		if (decorations != null)
			for (decoration in decorations) {
				if (decoration == null)
					throw "Decoration chains cannot contain null decorations";
				this.decorations.push(decoration.copy());
			}
	}

	public static function empty():DecorationChain
		return new DecorationChain();

	public static function of(decorations:Array<Decoration>):DecorationChain
		return new DecorationChain(decorations);

	public function copy():DecorationChain
		return new DecorationChain(decorations);

	public function isEqual(other:DecorationChain):Bool {
		if (other == null || decorations.length != other.decorations.length)
			return false;
		for (index in 0...decorations.length)
			if (!equalDecoration(decorations[index], other.decorations[index]))
				return false;
		return true;
	}

	/** Interpolates the ordered decoration values, preserving chain shape. */
	public static function interpolate(left:DecorationChain, right:DecorationChain,
			amount:Float):DecorationChain {
		if (left == null)
			return right == null ? empty() : right.copy();
		if (right == null)
			return left.copy();
		if (left.decorations.length != right.decorations.length)
			return amount < 0.5 ? left.copy() : right.copy();
		var result:Array<Decoration> = [];
		for (index in 0...left.decorations.length)
			result.push(left.decorations[index].interpolate(right.decorations[index], amount));
		return new DecorationChain(result);
	}

	/** Stable cache and inspection key for the resolved decoration sequence. */
	public function key():String {
		var values:Array<String> = [];
		for (decoration in decorations)
			values.push(decoration.describe());
		return values.join("|");
	}

	public function toString():String
		return "[" + key() + "]";

	static function equalDecoration(left:Decoration, right:Decoration):Bool {
		if (left == right)
			return true;
		return left.isEqual(right);
	}
}
