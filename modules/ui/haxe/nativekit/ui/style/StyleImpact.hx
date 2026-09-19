package nativekit.ui.style;

/** Work categories affected by a resolved style property. */
enum abstract StyleImpact(Int) from Int to Int {
	var None = 0;
	var Layout = 1 << 0;
	var TextLayout = 1 << 1;
	var Paint = 1 << 2;
	var Composite = 1 << 3;
	var Semantics = 1 << 4;
	/** Changes resolved visual geometry used by native picking without relayout. */
	var HitGeometry = 1 << 5;
}
