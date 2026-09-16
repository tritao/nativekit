package nativekit.ui.style;

import nativekit.ui.animation.Easing;

/** One property transition requested by a stylesheet. */
class StyleTransition {
	public final property:StyleProperty<Dynamic>;
	public final duration:Float;
	public final easing:Int;

	public function new(property:Dynamic, duration:Float, easing:Int = Easing.EaseOut) {
		if (property == null || duration < 0.0 || easing < Easing.Linear || easing > Easing.EaseInOut)
			throw "Style transitions require a property, duration, and valid easing";
		this.property = cast property;
		this.duration = duration;
		this.easing = easing;
	}
}
