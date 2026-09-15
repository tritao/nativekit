package nativekit.audio;

/** Minimum and maximum distances used by spatial attenuation. */
class DistanceLimits {
	public final min:Float;
	public final max:Float;

	public function new(min:Float, max:Float) {
		this.min = min;
		this.max = max;
	}
}
