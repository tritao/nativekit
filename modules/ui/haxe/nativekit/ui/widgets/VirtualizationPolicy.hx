package nativekit.ui.widgets;

/** Shared materialization policy for virtual collection controls. */
class VirtualizationPolicy {
	public final leadingOverscan:Int;
	public final trailingOverscan:Int;
	/** Reuses the outer row node while replacing its keyed item content. */
	public final recycleSlots:Bool;

	public function new(leadingOverscan:Int = 1, trailingOverscan:Int = 1,
			recycleSlots:Bool = true) {
		if (leadingOverscan < 0 || trailingOverscan < 0)
			throw "Virtualization overscan must be non-negative";
		this.leadingOverscan = leadingOverscan;
		this.trailingOverscan = trailingOverscan;
		this.recycleSlots = recycleSlots;
	}

	public function fixed(itemCount:Int, itemExtent:Float, viewportExtent:Float,
		offset:Float):VirtualViewport
		return new VirtualViewport(itemCount, itemExtent, viewportExtent, offset,
			leadingOverscan, trailingOverscan);

	public function variable(extents:Array<Float>, viewportExtent:Float,
		offset:Float):VirtualExtentViewport
		return new VirtualExtentViewport(extents, viewportExtent, offset,
			leadingOverscan, trailingOverscan);
}
