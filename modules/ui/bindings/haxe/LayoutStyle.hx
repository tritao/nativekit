/** Haxe-owned layout and paint policy for a semantic node. */
class LayoutStyle {
	public var width:LayoutAxis;
	public var height:LayoutAxis;
	public var direction:LayoutDirection;
	public var padding:Insets;
	public var childGap:Float;
	public var background:Color;
	public var radiusTopLeft:Float;
	public var radiusTopRight:Float;
	public var radiusBottomLeft:Float;
	public var radiusBottomRight:Float;
	public var clipHorizontal:Bool;
	public var clipVertical:Bool;

	public function new() {
		width = LayoutAxis.fit();
		height = LayoutAxis.fit();
		direction = LayoutDirection.TopToBottom;
		padding = new Insets(0.0, 0.0, 0.0, 0.0);
		childGap = 0.0;
		background = Color.rgba(0.0, 0.0, 0.0, 0.0);
		radiusTopLeft = 0.0;
		radiusTopRight = 0.0;
		radiusBottomLeft = 0.0;
		radiusBottomRight = 0.0;
		clipHorizontal = false;
		clipVertical = false;
	}
}
