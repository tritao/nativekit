package nativekit.ui.widgets;

/**
 * Computes a bounded materialization window for a fixed-extent collection.
 * The range is half-open: first is inclusive and last is exclusive.
 */
class VirtualViewport {
	public final itemCount:Int;
	public final itemExtent:Float;
	public final viewportExtent:Float;
	public final offset:Float;
	public final first:Int;
	public final last:Int;

	public var count(get, never):Int;
	inline function get_count():Int
		return last - first;

	/**
	 * Creates a fixed-extent materialization window. The defaults preserve the
	 * list behavior of one leading and one trailing overscan item.
	 */
	public function new(itemCount:Int, itemExtent:Float, viewportExtent:Float, offset:Float,
			leadingOverscan:Int = 1, trailingOverscan:Int = 1) {
		if (itemCount < 0 || itemExtent <= 0.0 || viewportExtent <= 0.0 ||
			!finite(itemExtent) || !finite(viewportExtent) || !finite(offset) || offset < 0.0 ||
			leadingOverscan < 0 || trailingOverscan < 0)
			throw "Virtual viewport requires finite non-negative dimensions and overscan";
		this.itemCount = itemCount;
		this.itemExtent = itemExtent;
		this.viewportExtent = viewportExtent;
		this.offset = clampOffset(itemCount, itemExtent, viewportExtent, offset);

		if (itemCount == 0) {
			first = 0;
			last = 0;
			return;
		}

		var visibleFirst = Std.int(this.offset / itemExtent);
		var visibleLast = Std.int((this.offset + viewportExtent) / itemExtent) + 1;
		first = Std.int(Math.max(0, visibleFirst - leadingOverscan));
		last = Std.int(Math.min(itemCount, visibleLast + trailingOverscan));
	}

	public function contains(index:Int):Bool
		return index >= first && index < last;

	static function clampOffset(itemCount:Int, itemExtent:Float, viewportExtent:Float,
			offset:Float):Float {
		var contentExtent = itemCount * itemExtent;
		var maxOffset = Math.max(0.0, contentExtent - viewportExtent);
		return Math.min(offset, maxOffset);
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
