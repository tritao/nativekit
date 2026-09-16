package testing;

/** Stable identities for the desktop Explorer smoke rotation. */
enum abstract ExplorerSmokeFrame(Int) from Int to Int {
	var Overview = 0;
	var Controls = 1;
	var Text = 2;
	var FocusedText = 3;
	var Layout = 4;
	var Lists = 5;
	var Overlays = 6;
	var Dialog = 7;
	var Popup = 8;
	var Menu = 9;
	var Graphics = 10;
	var Gestures = 11;
	var Select = 12;
}
