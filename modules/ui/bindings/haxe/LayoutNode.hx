/** Render/layout node submitted to NativeUI. Haxe semantics live above it. */
class LayoutNode {
	public final id:Int;
	public var visualKind:LayoutVisualKind;
	public final style:LayoutStyle;
	public var text:String;
	public var textColor:Color;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	/** Optional external content measured when this node is Custom. */
	public var intrinsicContent:Null<LayoutContent>;
	/** Invalidates native intrinsic measurement when no content object is attached. */
	public var measureVersion:Int;
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
		intrinsicContent = null;
		measureVersion = 0;
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
