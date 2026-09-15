package nativekit.audio;

/** Minimum and maximum gain applied by spatialization. */
class GainLimits {
	public final min:Float;
	public final max:Float;

	public function new(min:Float, max:Float) {
		this.min = min;
		this.max = max;
	}
}
