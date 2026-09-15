/** Host-owned intrinsic content that can participate in Custom-node layout. */
interface LayoutContent {
	/** Monotonically changing value used to invalidate the native measure cache. */
	function getVersion():Int;

	function measure(constraints:LayoutMeasureConstraints):LayoutMeasureResult;
}
