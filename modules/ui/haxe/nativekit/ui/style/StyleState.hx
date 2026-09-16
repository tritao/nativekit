package nativekit.ui.style;

/** Typed pseudo-state flags used by style selectors. */
enum abstract StyleState(Int) from Int to Int {
	var Hovered = 1 << 0;
	var Pressed = 1 << 1;
	var Focused = 1 << 2;
	var Disabled = 1 << 3;
	var Selected = 1 << 4;
	var Checked = 1 << 5;
}
