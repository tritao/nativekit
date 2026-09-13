package nativekit.ui.semantics;

/** Roles already understood by NativeKit's virtual accessibility tree. */
enum abstract AccessibilityRole(Int) from Int to Int {
	var Group = 0;
	var Button = 1;
	var Checkbox = 2;
	var Radio = 3;
	var Text = 4;
	var TextField = 5;
	var Link = 6;
	var Image = 7;
	var Heading = 8;
	var List = 9;
	var ListItem = 10;
	var Slider = 11;
	var ScrollArea = 12;
}
