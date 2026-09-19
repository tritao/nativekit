package nativekit.ui.core;

/** Rendering cache policy for a retained custom-paint plane. */
enum abstract CachePolicy(Int) from Int to Int {
	var None = 0;
	var Auto = 1;
	var Raster = 2;
}
