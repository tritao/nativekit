package nativekit.ui.core;

/** Platform touch details preserved on normalized pointer events. */
class UiTouchData {
	public final tool:Int;
	public final pressure:Float;
	public final tiltX:Float;
	public final tiltY:Float;

	public function new(tool:Int, pressure:Float, tiltX:Float, tiltY:Float) {
		this.tool = tool;
		this.pressure = pressure;
		this.tiltX = tiltX;
		this.tiltY = tiltY;
	}
}
