/** Semantic role used by a layout node. */
enum abstract LayoutNodeKind(Int) from Int to Int {
	var Box = 1;
	var Text = 2;
	var Button = 3;
}
