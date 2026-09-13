package nativekit.ui.core;

/** Haxe-owned focus and keyboard tab traversal policy. */
class FocusManager {
	var root:Null<RenderNode>;
	var order:Array<RenderNode>;
	var eligible:Map<Int, RenderNode>;
	var activeTrapId:Null<WidgetId>;
	var focusBeforeTrap:Null<WidgetId>;
	public var focusedId(default, null):Null<WidgetId>;

	public function new() {
		root = null;
		order = [];
		eligible = new Map();
		activeTrapId = null;
		focusBeforeTrap = null;
		focusedId = null;
	}

	public function rebuild(root:Null<RenderNode>):Void {
		this.root = root;
		order = [];
		eligible = new Map();
		var trap = findTrap(root);
		var nextTrapId = trap == null ? null : trap.id;
		if (trap != null) {
			if (activeTrapId == null || !activeTrapId.equals(nextTrapId)) {
				var current = focusedId == null || root == null ? null : root.find(focusedId);
				if (current == null || trap.find(current.id) == null)
					focusBeforeTrap = focusedId;
				activeTrapId = nextTrapId;
			}
			collect(trap, true);
		} else {
			var restoreFocus = false;
			if (activeTrapId != null) {
				activeTrapId = null;
				restoreFocus = true;
			}
			if (root != null)
				collect(root, true);
			if (restoreFocus) {
				if (focusBeforeTrap != null && eligible.exists(focusBeforeTrap.value))
					focusedId = eligible.get(focusBeforeTrap.value).id;
				focusBeforeTrap = null;
			}
		}
		if (trap != null && (focusedId == null || !eligible.exists(focusedId.value) ||
			trap.find(focusedId) == null))
			focusedId = order.length == 0 ? null : order[0].id;
		else if (focusedId != null && !eligible.exists(focusedId.value))
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

	function findTrap(node:Null<RenderNode>):Null<RenderNode> {
		if (node == null || node.resolved == null || !node.resolved.visible)
			return null;
		var result:Null<RenderNode> = node.focusTrap ? node : null;
		for (child in node.children) {
			var nested = findTrap(child);
			if (nested != null)
				result = nested;
		}
		return result;
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
