package nativekit.audio;

/** Cutoff frequency and order for a bus filter effect. */
class FilterSettings {
	public final cutoffFrequencyHz:Float;
	public final order:Int;

	public function new(cutoffFrequencyHz:Float, order:Int) {
		this.cutoffFrequencyHz = cutoffFrequencyHz;
		this.order = order;
	}
}
