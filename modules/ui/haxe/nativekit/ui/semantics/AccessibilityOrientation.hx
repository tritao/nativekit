package nativekit.ui.semantics;

/** Generic orientation values shared with NativeKit's accessibility ABI. */
enum abstract AccessibilityOrientation(Int) from Int to Int {
	var Unspecified = 0;
	var Horizontal = 1;
	var Vertical = 2;
}
