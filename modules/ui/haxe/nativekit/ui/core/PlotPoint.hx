package nativekit.ui.core;

/** One immutable sample in a telemetry/debug plot series. */
class PlotPoint {
	public final x:Float;
	public final y:Float;

	public function new(x:Float, y:Float) {
		if (!finite(x) || !finite(y))
			throw "Plot points must be finite";
		this.x = x;
		this.y = y;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
