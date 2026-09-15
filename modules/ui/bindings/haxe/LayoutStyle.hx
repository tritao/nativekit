/** Haxe-owned layout, paint, visibility, and transform data. */
class LayoutStyle {
	public var width:LayoutAxis;
	public var height:LayoutAxis;
	/** Width divided by height; zero disables aspect-ratio sizing. */
	public var aspectRatio:Float;
	public var direction:LayoutDirection;
	public var childAlignX:LayoutAlignmentX;
	public var childAlignY:LayoutAlignmentY;
	/** Main-axis free-space distribution policy. */
	public var childDistribution:LayoutDistribution;
	public var positioning:LayoutPositioning;
	/** Controls whether children flow onto additional rows or columns. */
	public var wrapMode:LayoutWrapMode;
	/** Vertical gap between wrapped rows. */
	public var rowGap:Float;
	/** Horizontal gap between wrapped columns. */
	public var columnGap:Float;
	public var positionX:Float;
	public var positionY:Float;
	public var zIndex:Int;
	public var clipToParent:Bool;
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
		aspectRatio = 0.0;
		direction = LayoutDirection.TopToBottom;
		childAlignX = LayoutAlignmentX.Start;
		childAlignY = LayoutAlignmentY.Start;
		childDistribution = LayoutDistribution.Start;
		positioning = LayoutPositioning.Flow;
		wrapMode = LayoutWrapMode.NoWrap;
		rowGap = 0.0;
		columnGap = 0.0;
		positionX = 0.0;
		positionY = 0.0;
		zIndex = 0;
		clipToParent = true;
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
		result.aspectRatio = aspectRatio;
		result.direction = direction;
		result.childAlignX = childAlignX;
		result.childAlignY = childAlignY;
		result.childDistribution = childDistribution;
		result.positioning = positioning;
		result.wrapMode = wrapMode;
		result.rowGap = rowGap;
		result.columnGap = columnGap;
		result.positionX = positionX;
		result.positionY = positionY;
		result.zIndex = zIndex;
		result.clipToParent = clipToParent;
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
