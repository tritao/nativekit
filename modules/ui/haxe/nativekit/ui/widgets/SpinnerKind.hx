package nativekit.ui.widgets;

/** Procedural visual used by Spinner. */
enum abstract SpinnerKind(Int) from Int to Int {
	/** A rotating open arc. */
	var Ring = 0;
	/** Twelve dots with a moving highlight. */
	var Dots = 1;
	/** Twelve radial bars with a moving highlight. */
	var Bars = 2;
	/** Two expanding and fading circles. */
	var Pulse = 3;
}
