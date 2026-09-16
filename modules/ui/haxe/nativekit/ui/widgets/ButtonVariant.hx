package nativekit.ui.widgets;

/** Semantic visual variants backed by theme-generated button state rules. */
enum abstract ButtonVariant(Int) from Int to Int {
	var Primary = 0;
	var Navigation = 1;
}
