package nativekit.ui.widgets;

/**
 * Estimated prefix metrics for a large variable-extent collection.
 *
 * Unknown items use the model's estimate. Measured items contribute a sparse
 * delta through a Fenwick tree, so prefix offsets remain logarithmic without
 * asking the model for every item up front.
 */
class VirtualExtentIndex {
	public final itemCount:Int;
	public final estimatedExtent:Float;
	public var viewportExtent(default, null):Float;
	public var offset(default, null):Float;
	public var first(default, null):Int;
	public var last(default, null):Int;

	final deltas:Array<Float>;
	final measured:Map<Int, Float>;
	final totalExtentOverride:Null<Float>;

	public var totalExtent(get, never):Float;
	inline function get_totalExtent():Float
		return totalExtentOverride == null ? itemCount * estimatedExtent + prefixDelta(itemCount) :
			cast totalExtentOverride;

	public var count(get, never):Int;
	inline function get_count():Int
		return last - first;

	public function new(itemCount:Int, estimatedExtent:Float, viewportExtent:Float, offset:Float,
			leadingOverscan:Int = 1, trailingOverscan:Int = 1,
			totalExtent:Null<Float> = null) {
		var invalidTotal = false;
		if (totalExtent != null) {
			var total:Float = cast totalExtent;
			invalidTotal = total < 0.0 || !finite(total) ||
				(itemCount > 0 && total <= 0.0);
		}
		if (itemCount < 0 || estimatedExtent <= 0.0 || !finite(estimatedExtent) ||
			viewportExtent <= 0.0 || !finite(viewportExtent) || !finite(offset) || offset < 0.0 ||
			leadingOverscan < 0 || trailingOverscan < 0 || invalidTotal)
			throw "Estimated virtual extent index requires finite dimensions and overscan";
		this.itemCount = itemCount;
		this.estimatedExtent = estimatedExtent;
		this.viewportExtent = viewportExtent;
		this.offset = 0.0;
		this.first = 0;
		this.last = 0;
		this.totalExtentOverride = totalExtent;
		deltas = [];
		for (_ in 0...(itemCount + 1))
			deltas.push(0.0);
		measured = new Map();
		update(viewportExtent, offset, leadingOverscan, trailingOverscan);
	}

	/** Applies a measured extent and updates all following prefix offsets. */
	public function setExtent(index:Int, extent:Float):Void {
		if (index < 0 || index >= itemCount || extent <= 0.0 || !finite(extent))
			throw "Estimated virtual extent index received an invalid item extent";
		var previous = measured.exists(index) ? measured.get(index) : estimatedExtent;
		measured.set(index, extent);
		var delta = extent - previous;
		if (delta == 0.0)
			return;
		var cursor = index + 1;
		while (cursor <= itemCount) {
			deltas[cursor] += delta;
			cursor += (cursor & -cursor);
		}
	}

	public function extentAt(index:Int):Float {
		if (index < 0 || index >= itemCount)
			throw "Estimated virtual extent index is out of range";
		var value = measured.get(index);
		return value == null ? estimatedExtent : value;
	}

	/** Repositions the window using estimated and measured prefix metrics. */
	public function update(viewportExtent:Float, offset:Float,
			leadingOverscan:Int = 1, trailingOverscan:Int = 1):Void {
		if (viewportExtent <= 0.0 || !finite(viewportExtent) || !finite(offset) || offset < 0.0 ||
			leadingOverscan < 0 || trailingOverscan < 0)
			throw "Estimated virtual extent index requires finite dimensions and overscan";
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

	/** Returns the estimated/measured start offset, or content end at itemCount. */
	public function startOffset(index:Int):Float {
		if (index < 0 || index > itemCount)
			throw "Estimated virtual extent offset index is out of range";
		if (index == itemCount && totalExtentOverride != null)
			return cast totalExtentOverride;
		return index * estimatedExtent + prefixDelta(index);
	}

	/** Returns the item containing an offset, clamped to the last item. */
	public function indexAtOffset(value:Float):Int {
		if (!finite(value) || value < 0.0)
			throw "Estimated virtual extent offset must be finite and non-negative";
		if (itemCount == 0)
			return 0;
		var clamped = Math.min(value, Math.max(0.0, totalExtent));
		var index = upperBound(clamped) - 1;
		return Std.int(Math.max(0, Math.min(itemCount - 1, index)));
	}

	function upperBound(value:Float):Int {
		var low = 0;
		var high = itemCount + 1;
		while (low < high) {
			var middle = (low + high) >> 1;
			if (startOffset(middle) <= value)
				low = middle + 1;
			else
				high = middle;
		}
		return low;
	}

	function prefixDelta(count:Int):Float {
		var result = 0.0;
		var cursor = count;
		while (cursor > 0) {
			result += deltas[cursor];
			cursor -= (cursor & -cursor);
		}
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
