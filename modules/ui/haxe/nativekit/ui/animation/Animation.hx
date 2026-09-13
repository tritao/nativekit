package nativekit.ui.animation;

/** One Haxe-side animation advanced by the UiContext frame clock. */
interface Animation {
	/** Advances by a non-negative duration and returns whether it remains active. */
	function advance(deltaSeconds:Float):Bool;
}
