package nativekit.ui.core;

import LayoutPositioning;

/** Hit testing and target paths derived from the resolved Haxe render tree. */
class HitTest {
	public static function path(root:Null<RenderNode>, x:Float, y:Float):Array<RenderNode> {
		if (root == null)
			return [];
		var result:Array<RenderNode> = [];
		return visit(root, x, y, result) ? result : [];
	}

	public static function pathTo(node:Null<RenderNode>):Array<RenderNode> {
		if (node == null)
			return [];
		var result:Array<RenderNode> = [];
		var current:Null<RenderNode> = node;
		while (current != null) {
			var present:RenderNode = cast current;
			result.push(present);
			current = present.parent;
		}
		result.reverse();
		return result;
	}

	static function visit(node:RenderNode, x:Float, y:Float, path:Array<RenderNode>):Bool {
		var geometry = node.resolved;
		if (geometry == null || !geometry.visible || !inside(geometry.clipBounds, x, y))
			return false;
		path.push(node);
		var ordered:Array<RenderNode> = [];
		for (child in node.children) {
			var zIndex = child.layout.style.positioning == LayoutPositioning.Absolute
				? child.layout.style.zIndex : 0;
			var index = ordered.length;
			while (index > 0 && zIndex < childZIndex(ordered[index - 1]))
				index--;
			ordered.insert(index, child);
		}
		var index = ordered.length - 1;
		while (index >= 0) {
			if (visit(ordered[index], x, y, path))
				return true;
			index--;
		}
		if (geometry.hitTest(x, y))
			return true;
		path.pop();
		return false;
	}

	static inline function inside(rect:Rect, x:Float, y:Float):Bool
		return x >= rect.x && y >= rect.y && x <= rect.x + rect.width && y <= rect.y + rect.height;

	static inline function childZIndex(node:RenderNode):Int
		return node.layout.style.positioning == LayoutPositioning.Absolute
			? node.layout.style.zIndex : 0;
}
