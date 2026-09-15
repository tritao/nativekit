/** Cross-axis alignment for children in a left-to-right layout. */
enum abstract LayoutAlignmentY(Int) from Int to Int {
	var Start = 0;
	var End = 1;
	var Center = 2;
	/** Aligns baselines; baseline-less children use their bottom edge. */
	var Baseline = 3;
}
