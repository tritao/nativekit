/** Haxe-owned layout, paint, visibility, and transform data. */
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
	public var visible:Bool;
	public var transform:Transform2D;

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
		visible = true;
		transform = Transform2D.identity();
	}

	/** Returns an independent style value for compositional widget builders. */
	public function copy():LayoutStyle {
		var result = new LayoutStyle();
		result.width = width;
		result.height = height;
		result.direction = direction;
		result.padding = new Insets(padding.left, padding.top, padding.right, padding.bottom);
		result.childGap = childGap;
		result.background = background;
		result.radiusTopLeft = radiusTopLeft;
		result.radiusTopRight = radiusTopRight;
		result.radiusBottomLeft = radiusBottomLeft;
		result.radiusBottomRight = radiusBottomRight;
		result.clipHorizontal = clipHorizontal;
		result.clipVertical = clipVertical;
		result.visible = visible;
		result.transform = new Transform2D(transform.a, transform.b, transform.c,
			transform.d, transform.tx, transform.ty);
		return result;
	}
}
