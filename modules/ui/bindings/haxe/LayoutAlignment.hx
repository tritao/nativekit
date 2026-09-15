/** Alignment applied to children along one parent axis. */
enum abstract LayoutAlignment(Int) from Int to Int {
	var Start = 0;
	var End = 1;
	var Center = 2;
	/** Aligns baselines in horizontal rows; baseline-less children use their bottom edge. */
	var Baseline = 3;
}
