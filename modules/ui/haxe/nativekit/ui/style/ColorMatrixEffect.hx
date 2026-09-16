package nativekit.ui.style;

/** Four-channel color matrix in row-major 4x5 form. */
class ColorMatrixEffect extends Effect {
	public static inline var ComponentCount:Int = 20;
	public final matrix:Array<Float>;

	public function new(matrix:Array<Float>) {
		super(EffectKind.ColorMatrix);
		if (matrix == null || matrix.length != ComponentCount)
			throw "Color matrices require 20 components";
		this.matrix = [];
		for (value in matrix)
			this.matrix.push(Effect.requireFinite(value,
				"Color matrix components must be finite"));
	}

	public static function identity():ColorMatrixEffect {
		var matrix:Array<Float> = [];
		for (row in 0...4)
			for (column in 0...5)
				matrix.push(row == column ? 1.0 : 0.0);
		return new ColorMatrixEffect(matrix);
	}

	override public function isEqual(other:Effect):Bool {
		if (other == null || other.kind != EffectKind.ColorMatrix)
			return false;
		var value:ColorMatrixEffect = cast other;
		if (value == null || matrix.length != value.matrix.length)
			return false;
		for (index in 0...matrix.length)
			if (matrix[index] != value.matrix[index])
				return false;
		return true;
	}

	override public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || other.kind != EffectKind.ColorMatrix)
			return Effect.discrete(this, other, amount);
		var value:ColorMatrixEffect = cast other;
		var result:Array<Float> = [];
		for (index in 0...matrix.length)
			result.push(Effect.interpolateFloat(matrix[index], value.matrix[index], amount));
		return new ColorMatrixEffect(result);
	}

	override public function copy():Effect
		return new ColorMatrixEffect(matrix);

	override public function describe():String {
		var values:Array<String> = [];
		for (value in matrix)
			values.push(Std.string(value));
		return 'color-matrix(' + values.join(",") + ')';
	}
}
