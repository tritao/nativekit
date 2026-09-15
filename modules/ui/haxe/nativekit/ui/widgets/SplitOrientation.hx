package nativekit.ui.widgets;

/** Main axis used by a SplitView. */
enum abstract SplitOrientation(Int) from Int to Int {
	var Horizontal = 0;
	var Vertical = 1;
}
