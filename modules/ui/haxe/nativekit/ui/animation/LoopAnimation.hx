package nativekit.ui.animation;

/** Repeating normalized phase advanced by the Haxe UI frame clock. */
class LoopAnimation implements Animation {
	public var phase(default, null):Float;
	public var speed:Float;

	public function new(speed:Float = 1.0) {
		if (!finite(speed) || speed <= 0.0)
			throw "Loop animation speed must be finite and positive";
		this.speed = speed;
		phase = 0.0;
	}

	public function advance(deltaSeconds:Float):Bool {
		if (!finite(deltaSeconds) || deltaSeconds < 0.0)
			throw "Loop animation time must be finite and non-negative";
		phase += deltaSeconds * speed;
		phase -= Std.int(phase);
		return true;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
