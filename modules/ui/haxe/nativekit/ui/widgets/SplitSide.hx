package nativekit.ui.widgets;

/** Which edge contains a SplitView's resizable pane. */
enum abstract SplitSide(Int) from Int to Int {
	var Leading = 0;
	var Trailing = 1;
}
