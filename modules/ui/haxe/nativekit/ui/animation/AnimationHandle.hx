package nativekit.ui.animation;

/** Stable registration returned by AnimationScheduler.track. */
class AnimationHandle {
	final scheduler:AnimationScheduler;
	final animation:Animation;
	public var active(default, null):Bool;

	@:allow(nativekit.ui.animation.AnimationScheduler)
	private function new(scheduler:AnimationScheduler, animation:Animation) {
		this.scheduler = scheduler;
		this.animation = animation;
		active = true;
	}

	/** Removes this animation from its scheduler. Repeated cancellation is safe. */
	public function cancel():Void {
		if (active)
			scheduler.removeHandle(this);
	}

	@:allow(nativekit.ui.animation.AnimationScheduler)
	function advance(deltaSeconds:Float):Bool
		return animation.advance(deltaSeconds);

	@:allow(nativekit.ui.animation.AnimationScheduler)
	function deactivate():Void
		active = false;

	@:allow(nativekit.ui.animation.AnimationScheduler)
	function matches(candidate:Animation):Bool
		return animation == candidate;
}
