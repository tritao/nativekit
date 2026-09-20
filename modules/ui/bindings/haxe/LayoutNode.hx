/** Render/layout node submitted to NativeUI. Haxe semantics live above it. */
class LayoutNode {
	public final id:Int;
	public var visualKind:LayoutVisualKind;
	public final style:LayoutStyle;
	public var text:String;
	public var textColor:Color;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	/** Whether this node's own resolved bounds participate in geometric picking. */
	public var hitSelf:Bool;
	/** Whether descendants participate in geometric picking below this node. */
	public var hitChildren:Bool;
	/** Optional external content measured when this node is Custom. */
	public var intrinsicContent:Null<LayoutContent>;
	/** Invalidates native intrinsic measurement when no content object is attached. */
	public var measureVersion:Int;
	/** Paint/text revision consumed by native raster-cache identity. */
	public var contentRevision:Int;
	/** Resolved bounds/transform/clip revision shared by native scene consumers. */
	public var geometryRevision:Int;
	/** Opacity/effects revision shared by native scene consumers. */
	public var compositeRevision:Int;
	public final children:Array<LayoutNode>;

	public function new(id:Int, visualKind:LayoutVisualKind = LayoutVisualKind.Box,
			?style:LayoutStyle) {
		if (id <= 0)
			throw "Layout node IDs must be positive";
		this.id = id;
		this.visualKind = visualKind;
		this.style = style == null ? new LayoutStyle() : style;
		text = "";
		textColor = Color.rgba(1.0, 1.0, 1.0, 1.0);
		textStyle = new TextStyle();
		paragraphStyle = new ParagraphStyle();
		hitSelf = true;
		hitChildren = true;
		intrinsicContent = null;
		measureVersion = 0;
		contentRevision = 0;
		geometryRevision = 0;
		compositeRevision = 0;
		children = [];
	}

	public function add(child:LayoutNode):LayoutNode {
		if (child == null || child == this)
			throw "A layout node cannot contain itself";
		children.push(child);
		return child;
	}

	public static function box(id:Int, ?style:LayoutStyle):LayoutNode
		return new LayoutNode(id, LayoutVisualKind.Box, style);

	public static function textNode(id:Int, value:String, ?style:LayoutStyle):LayoutNode {
		var node = new LayoutNode(id, LayoutVisualKind.Text, style);
		node.text = value == null ? "" : value;
		return node;
	}

	/** Creates a Custom node backed by a reusable intrinsic content provider. */
	public static function custom(id:Int, content:LayoutContent, ?style:LayoutStyle):LayoutNode {
		if (content == null)
			throw "Custom layout nodes require intrinsic content";
		var node = new LayoutNode(id, LayoutVisualKind.Custom, style);
		node.intrinsicContent = content;
		return node;
	}
}
