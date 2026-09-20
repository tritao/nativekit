package nativekit.ui.widgets;

/**
 * Computes a bounded materialization window for variable-extent items.
 * Offsets are indexed by item start, and the materialized range is half-open.
 */
class VirtualExtentViewport {
	public final itemCount:Int;
	public var viewportExtent(default, null):Float;
	public var offset(default, null):Float;
	public final totalExtent:Float;
	public var first(default, null):Int;
	public var last(default, null):Int;
	final offsets:Array<Float>;

	public var count(get, never):Int;
	inline function get_count():Int
		return last - first;

	public function new(extents:Array<Float>, viewportExtent:Float, offset:Float,
			leadingOverscan:Int = 1, trailingOverscan:Int = 1) {
		if (extents == null || viewportExtent <= 0.0 || !finite(viewportExtent) ||
			!finite(offset) || offset < 0.0 || leadingOverscan < 0 || trailingOverscan < 0)
			throw "Variable virtual viewport requires finite dimensions and overscan";
		itemCount = extents.length;
		var builtOffsets:Array<Float> = [0.0];
		for (extent in extents) {
			if (extent <= 0.0 || !finite(extent))
				throw "Variable virtual viewport extents must be finite and positive";
			var nextOffset = builtOffsets[builtOffsets.length - 1] + extent;
			if (!finite(nextOffset))
				throw "Variable virtual viewport total extent must be finite";
			builtOffsets.push(nextOffset);
		}
		offsets = builtOffsets;
		totalExtent = offsets[offsets.length - 1];
		update(viewportExtent, offset, leadingOverscan, trailingOverscan);
	}

	/** Repositions this extent index without rebuilding its prefix-offset cache. */
	public function update(viewportExtent:Float, offset:Float,
			leadingOverscan:Int = 1, trailingOverscan:Int = 1):Void {
		if (viewportExtent <= 0.0 || !finite(viewportExtent) || !finite(offset) || offset < 0.0 ||
			leadingOverscan < 0 || trailingOverscan < 0)
			throw "Variable virtual viewport requires finite dimensions and overscan";
		this.viewportExtent = viewportExtent;
		this.offset = Math.min(offset, Math.max(0.0, totalExtent - viewportExtent));
		if (itemCount == 0) {
			first = 0;
			last = 0;
			return;
		}

		var visibleFirst = upperBound(this.offset) - 1;
		var visibleLast = upperBound(this.offset + viewportExtent);
		first = Std.int(Math.max(0, visibleFirst - leadingOverscan));
		last = Std.int(Math.min(itemCount, visibleLast + trailingOverscan));
	}

	/** Returns the item start offset, or the content end for itemCount. */
	public function startOffset(index:Int):Float {
		if (index < 0 || index > itemCount)
			throw "Virtual viewport offset index is out of range";
		return offsets[index];
	}

	public function contains(index:Int):Bool
		return index >= first && index < last;

	function upperBound(value:Float):Int {
		var low = 0;
		var high = offsets.length;
		while (low < high) {
			var middle = (low + high) >> 1;
			if (offsets[middle] <= value)
				low = middle + 1;
			else
				high = middle;
		}
		return low;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
