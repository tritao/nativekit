package testing;

/** Stable numeric identities retained for the ShowcaseWeb screenshot ABI. */
enum abstract ExplorerVisualCase(Int) from Int to Int {
	var Overview = 0;
	var Controls = 1;
	var ControlsLight = 2;
	var ControlsFocused = 3;
	var TextFocused = 4;
	var Layout = 5;
	var ListsScrolled = 6;
	var Dialog = 7;
	var Popup = 8;
	var Menu = 9;
	var ControlsCompact = 10;
	var Gestures = 11;
	var ListsLight = 12;
	var TextAreaSelection = 13;
	var MenuLight = 14;
}
