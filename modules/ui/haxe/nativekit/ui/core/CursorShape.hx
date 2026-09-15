package nativekit.ui.core;

/** Platform-independent cursor intent for a render node. */
enum abstract CursorShape(Int) from Int to Int {
	var Arrow = 0;
	var Text = 1;
	var Crosshair = 2;
	var Hand = 3;
	var HorizontalResize = 4;
	var VerticalResize = 5;
	var DiagonalResize = 6;
	var Move = 7;
	var NotAllowed = 8;
}
