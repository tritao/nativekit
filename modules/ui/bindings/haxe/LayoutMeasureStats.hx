/** Snapshot of intrinsic measurement and persistent-cache activity. */
class LayoutMeasureStats {
	public final requests:haxe.Int64;
	public final cacheHits:haxe.Int64;
	public final cacheMisses:haxe.Int64;
	public final callbackCalls:haxe.Int64;
	public final cacheEntries:haxe.Int64;
	public final cacheCapacity:haxe.Int64;

	public function new(requests:haxe.Int64, cacheHits:haxe.Int64, cacheMisses:haxe.Int64,
			callbackCalls:haxe.Int64, cacheEntries:haxe.Int64, cacheCapacity:haxe.Int64) {
		this.requests = requests;
		this.cacheHits = cacheHits;
		this.cacheMisses = cacheMisses;
		this.callbackCalls = callbackCalls;
		this.cacheEntries = cacheEntries;
		this.cacheCapacity = cacheCapacity;
	}
}
