/** Rendering/layout representation, independent from semantic role. */
enum abstract LayoutVisualKind(Int) from Int to Int {
	var Box = 1;
	var Text = 2;
	var Image = 3;
	var Custom = 4;
}
