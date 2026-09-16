/** Texture sampling used when an image is drawn larger or smaller than its source. */
enum abstract ImageFilter(Int) from Int to Int {
	var Linear = 1;
	var Nearest = 2;
}
