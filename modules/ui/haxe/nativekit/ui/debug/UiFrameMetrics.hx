package nativekit.ui.debug;

/** Timing and style-cache counters captured for the most recent UI frame. */
class UiFrameMetrics {
	public final frameNumber:Int;
	public final nodeCount:Int;
	public final styleResolutions:Int;
	public final styleCacheHits:Int;
	public final styleCacheMisses:Int;
	public final cachedStyleCount:Int;
	public final submitSeconds:Float;
	public var renderSeconds(default, null):Float;
	public var totalSeconds(default, null):Float;

	public function new(frameNumber:Int, nodeCount:Int, styleResolutions:Int,
			styleCacheHits:Int, styleCacheMisses:Int, cachedStyleCount:Int,
			submitSeconds:Float) {
		this.frameNumber = frameNumber;
		this.nodeCount = nodeCount;
		this.styleResolutions = styleResolutions;
		this.styleCacheHits = styleCacheHits;
		this.styleCacheMisses = styleCacheMisses;
		this.cachedStyleCount = cachedStyleCount;
		this.submitSeconds = submitSeconds;
		renderSeconds = 0.0;
		totalSeconds = submitSeconds;
	}

	/** Completes the frame record once native paint submission has finished. */
	public function completeRender(seconds:Float):Void {
		renderSeconds = seconds < 0.0 ? 0.0 : seconds;
		totalSeconds = submitSeconds + renderSeconds;
	}

	public function cacheHitRate():Float {
		var lookups = styleCacheHits + styleCacheMisses;
		return lookups == 0 ? 0.0 : styleCacheHits / lookups;
	}
}
