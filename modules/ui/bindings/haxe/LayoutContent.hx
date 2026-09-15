/** Host-owned intrinsic content that can participate in Custom-node layout. */
interface LayoutContent {
	/** Application-defined cache token; change it whenever intrinsic metrics change. */
	function getVersion():Int;

	function measure(constraints:LayoutMeasureConstraints):LayoutMeasureResult;
}
