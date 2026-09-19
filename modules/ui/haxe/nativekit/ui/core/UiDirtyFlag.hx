package nativekit.ui.core;

import nativekit.ui.style.StyleImpact;

/** Work categories used by the style/layout invalidation boundary. */
class UiDirtyFlag {
	public static inline var None:Int = 0;
	public static inline var NeedsBuild:Int = 1 << 0;
	public static inline var NeedsStyle:Int = 1 << 1;
	public static inline var NeedsTextLayout:Int = 1 << 2;
	public static inline var NeedsLayout:Int = 1 << 3;
	public static inline var NeedsPaint:Int = 1 << 4;
	public static inline var NeedsComposite:Int = 1 << 5;
	public static inline var NeedsSemantics:Int = 1 << 6;
	/** Resolved transforms, clips, bounds, or paint order must be refreshed. */
	public static inline var NeedsHitGeometry:Int = 1 << 7;

	public static function fromStyleImpact(impact:StyleImpact):Int {
		var result = 0;
		if ((impact & StyleImpact.Layout) != 0) result |= NeedsLayout;
		if ((impact & StyleImpact.TextLayout) != 0) result |= NeedsTextLayout;
		if ((impact & StyleImpact.Paint) != 0) result |= NeedsPaint;
		if ((impact & StyleImpact.Composite) != 0) result |= NeedsComposite;
		if ((impact & StyleImpact.Semantics) != 0) result |= NeedsSemantics;
		if ((impact & StyleImpact.HitGeometry) != 0) result |= NeedsHitGeometry;
		return result;
	}

	public static inline function contains(flags:Int, flag:Int):Bool
		return (flags & flag) != 0;
}
