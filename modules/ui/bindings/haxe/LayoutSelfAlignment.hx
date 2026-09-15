/** Optional per-child cross-axis alignment override. */
enum abstract LayoutSelfAlignment(Int) from Int to Int {
	var Inherit = 0;
	var Start = 1;
	var End = 2;
	var Center = 3;
	/** Aligns baselines in a left-to-right parent. */
	var Baseline = 4;
}
