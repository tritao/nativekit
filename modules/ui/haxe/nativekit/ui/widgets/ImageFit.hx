package nativekit.ui.widgets;

/** Controls how an image's intrinsic aspect ratio maps into its view bounds. */
enum abstract ImageFit(Int) from Int to Int {
	var Stretch = 0;
	var Contain = 1;
	var Cover = 2;
	var None = 3;
}
