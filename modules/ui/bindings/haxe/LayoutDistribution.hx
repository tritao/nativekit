/** Main-axis free-space distribution policy. */
enum abstract LayoutDistribution(Int) from Int to Int {
	var Start = 0;
	var Center = 1;
	var End = 2;
	var SpaceBetween = 3;
	var SpaceAround = 4;
	var SpaceEvenly = 5;
}
