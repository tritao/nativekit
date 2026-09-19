/** Snapshot of native geometric hit-test traversal activity. */
class LayoutHitTestStats {
	public final hitTestCount:haxe.Int64;
	public final nodesVisited:haxe.Int64;
	public final subtreesRejected:haxe.Int64;
	public final preciseHitTests:haxe.Int64;
	public final maxNodesVisited:haxe.Int64;
	public final hitTestTimeNanoseconds:haxe.Int64;

	public function new(hitTestCount:haxe.Int64, nodesVisited:haxe.Int64,
			subtreesRejected:haxe.Int64, preciseHitTests:haxe.Int64,
			maxNodesVisited:haxe.Int64, hitTestTimeNanoseconds:haxe.Int64) {
		this.hitTestCount = hitTestCount;
		this.nodesVisited = nodesVisited;
		this.subtreesRejected = subtreesRejected;
		this.preciseHitTests = preciseHitTests;
		this.maxNodesVisited = maxNodesVisited;
		this.hitTestTimeNanoseconds = hitTestTimeNanoseconds;
	}
}
