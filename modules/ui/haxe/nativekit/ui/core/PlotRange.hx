package nativekit.ui.core;

/** Finite data range used to map plot samples into a viewport. */
class PlotRange {
	public final minimumX:Float;
	public final maximumX:Float;
	public final minimumY:Float;
	public final maximumY:Float;

	public function new(minimumX:Float, maximumX:Float, minimumY:Float, maximumY:Float) {
		this.minimumX = minimumX;
		this.maximumX = maximumX;
		this.minimumY = minimumY;
		this.maximumY = maximumY;
	}
}
