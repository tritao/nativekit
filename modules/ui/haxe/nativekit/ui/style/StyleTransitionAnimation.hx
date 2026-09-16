package nativekit.ui.style;

import nativekit.ui.animation.Animation;
import nativekit.ui.animation.Easing;
import Color;

/** Scheduler adapter for one dynamically typed style interpolation. */
class StyleTransitionAnimation implements Animation {
	final from:Dynamic;
	final to:Dynamic;
	final duration:Float;
	final easing:Int;
	final propertyName:String;
	final onUpdate:Dynamic->Void;
	final onComplete:Void->Void;
	var elapsed:Float;

	public function new(from:Dynamic, to:Dynamic, duration:Float, easing:Int,
			propertyName:String, onUpdate:Dynamic->Void, onComplete:Void->Void) {
		this.from = from;
		this.to = to;
		this.duration = duration;
		this.easing = easing;
		this.propertyName = propertyName;
		this.onUpdate = onUpdate;
		this.onComplete = onComplete;
		elapsed = 0.0;
	}

	public function advance(deltaSeconds:Float):Bool {
		elapsed += deltaSeconds;
		var complete = duration <= 0.0 || elapsed >= duration;
		var amount = complete ? 1.0 : Easing.apply(elapsed / duration, easing);
		onUpdate(complete ? to : StyleInterpolator.interpolate(propertyName, from, to, amount));
		if (complete) {
			onComplete();
			return false;
		}
		return true;
	}
}

/** Haxeon-friendly interpolation implementation for the metadata-supported values. */
class StyleInterpolator {
	public static function interpolate(propertyName:String, from:Dynamic, to:Dynamic,
			amount:Float):Dynamic {
		return switch propertyName {
			case "background" | "borderColor" | "outlineColor" | "shadowColor":
				var fromColor:Color = cast from;
				var toColor:Color = cast to;
				Color.rgba(fromColor.red + (toColor.red - fromColor.red) * amount,
					fromColor.green + (toColor.green - fromColor.green) * amount,
					fromColor.blue + (toColor.blue - fromColor.blue) * amount,
					fromColor.alpha + (toColor.alpha - fromColor.alpha) * amount);
			default:
				var fromFloat:Float = cast from;
				var toFloat:Float = cast to;
				fromFloat + (toFloat - fromFloat) * amount;
		};
	}
}
