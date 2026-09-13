/** Whether a node participates in parent flow or floats over sibling layout. */
enum abstract LayoutPositioning(Int) from Int to Int {
	var Flow = 0;
	var Absolute = 1;
}
