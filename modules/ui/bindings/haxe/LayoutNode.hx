/** Retained semantic node owned by the Haxe component tree. */
class LayoutNode {
	public final id:Int;
	public var kind:LayoutNodeKind;
	public final style:LayoutStyle;
	public var text:String;
	public var textColor:Color;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	public final children:Array<LayoutNode>;

	public function new(id:Int, kind:LayoutNodeKind = LayoutNodeKind.Box,
			?style:LayoutStyle) {
		if (id <= 0)
			throw "Layout node IDs must be positive";
		this.id = id;
		this.kind = kind;
		this.style = style == null ? new LayoutStyle() : style;
		text = "";
		textColor = Color.rgba(1.0, 1.0, 1.0, 1.0);
		textStyle = new TextStyle();
		paragraphStyle = new ParagraphStyle();
		children = [];
	}

	public function add(child:LayoutNode):LayoutNode {
		if (child == null || child == this)
			throw "A layout node cannot contain itself";
		children.push(child);
		return child;
	}

	public static function box(id:Int, ?style:LayoutStyle):LayoutNode
		return new LayoutNode(id, LayoutNodeKind.Box, style);

	public static function button(id:Int, ?style:LayoutStyle):LayoutNode
		return new LayoutNode(id, LayoutNodeKind.Button, style);

	public static function textNode(id:Int, value:String, ?style:LayoutStyle):LayoutNode {
		var node = new LayoutNode(id, LayoutNodeKind.Text, style);
		node.text = value == null ? "" : value;
		return node;
	}
}
