package nativekit.ui.core;

/** Haxe-owned focus and keyboard tab traversal policy. */
class FocusManager {
	var root:Null<RenderNode>;
	var order:Array<RenderNode>;
	var eligible:Map<Int, RenderNode>;
	public var focusedId(default, null):Null<WidgetId>;

	public function new() {
		root = null;
		order = [];
		eligible = new Map();
		focusedId = null;
	}

	public function rebuild(root:Null<RenderNode>):Void {
		this.root = root;
		order = [];
		eligible = new Map();
		if (root != null)
			collect(root, true);
		if (focusedId != null && !eligible.exists(focusedId.value))
			focusedId = null;
	}

	public function focus(id:Null<WidgetId>):Bool {
		if (id == null) {
			focusedId = null;
			return true;
		}
		var node = eligible.get(id.value);
		if (node == null)
			return false;
		focusedId = node.id;
		return true;
	}

	public function focusNext():Null<WidgetId>
		return move(1);

	public function focusPrevious():Null<WidgetId>
		return move(-1);

	public function focusedNode():Null<RenderNode>
		return focusedId == null ? null : find(focusedId);

	public function contains(id:WidgetId):Bool
		return find(id) != null;

	function move(direction:Int):Null<WidgetId> {
		if (order.length == 0) {
			focusedId = null;
			return null;
		}
		var current = -1;
		for (index in 0...order.length)
			if (focusedId != null && order[index].id.equals(focusedId)) {
				current = index;
				break;
			}
		var next = current < 0 ? (direction > 0 ? 0 : order.length - 1) : current + direction;
		if (next < 0)
			next = order.length - 1;
		else if (next >= order.length)
			next = 0;
		focusedId = order[next].id;
		return focusedId;
	}

	function collect(node:RenderNode, ancestorsEnabled:Bool):Void {
		var visible = node.resolved != null && node.resolved.visible;
		var enabled = ancestorsEnabled && node.enabled && visible;
		if (enabled && node.focusable) {
			eligible.set(node.id.value, node);
			if (node.tabIndex >= 0)
				insertOrdered(node);
		}
		if (visible)
			for (child in node.children)
				collect(child, enabled);
	}

	function insertOrdered(node:RenderNode):Void {
		var index = order.length;
		while (index > 0 && before(node, order[index - 1]))
			index--;
		order.insert(index, node);
	}

	static function before(left:RenderNode, right:RenderNode):Bool {
		if (left.tabIndex > 0 && right.tabIndex == 0)
			return true;
		if (left.tabIndex == 0 && right.tabIndex > 0)
			return false;
		return left.tabIndex > 0 && right.tabIndex > 0 && left.tabIndex < right.tabIndex;
	}

	function find(id:WidgetId):Null<RenderNode>
		return root == null || id == null ? null : root.find(id);
}
