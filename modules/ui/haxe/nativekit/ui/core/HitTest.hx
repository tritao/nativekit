package nativekit.ui.core;

import LayoutPositioning;

/** Hit testing and target paths derived from the resolved Haxe render tree. */
class HitTest {
	public static function path(root:Null<RenderNode>, x:Float, y:Float):Array<RenderNode> {
		if (root == null)
			return [];
		var state = new HitTestState(x, y);
		state.visit(root, [], null, -1);
		return state.bestPath == null ? [] : state.bestPath;
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
}

/**
 * Mirrors Clay's paint roots for hit testing. Normal flow content belongs to
 * the base root (z 0); an absolute node starts a separate stable z-layer and
 * its non-absolute descendants inherit that layer. Candidates are collected
 * in declaration/DFS order, then the last paint candidate wins.
 */
private class HitTestState {
	final x:Float;
	final y:Float;
	var sequence:Int;
	public var bestPath:Null<Array<RenderNode>>;
	var bestLayerZ:Int;
	var bestLayerKind:Int;
	var bestLayerOrder:Int;
	var bestCandidateOrder:Int;

	public function new(x:Float, y:Float) {
		this.x = x;
		this.y = y;
		sequence = 0;
		bestPath = null;
		bestLayerZ = 0;
		bestLayerKind = 0;
		bestLayerOrder = -1;
		bestCandidateOrder = -1;
	}

	public function visit(node:RenderNode, path:Array<RenderNode>,
			stackingOwner:Null<RenderNode>, stackingOwnerOrder:Int):Void {
		var nodeOrder = sequence++;
		var geometry = node.resolved;
		if (geometry == null || !geometry.visible || !inside(geometry.clipBounds, x, y))
			return;
		var nextPath = path.copy();
		nextPath.push(node);
		var behavior = node.hitTestBehavior;
		if (behavior == HitTestBehavior.None)
			return;

		var owner = stackingOwner;
		var ownerOrder = stackingOwnerOrder;
		if (node.layout.style.positioning == LayoutPositioning.Absolute) {
			owner = node;
			ownerOrder = nodeOrder;
		}
		var testSelf = behavior != HitTestBehavior.ChildrenOnly && node.hitTestSelf;
		if (testSelf && geometry.hitTest(x, y))
			consider(nextPath, owner, ownerOrder, sequence++);

		if (behavior == HitTestBehavior.SelfOnly)
			return;
		for (child in node.children)
			visit(child, nextPath, owner, ownerOrder);
	}

	function consider(path:Array<RenderNode>, owner:Null<RenderNode>, ownerOrder:Int,
			candidateOrder:Int):Void {
		var layerZ = owner == null ? 0 : owner.layout.style.zIndex;
		// Clay's base flow root is emitted before floating roots at the same
		// z-index. Negative/positive z-index values still compare normally.
		var layerKind = owner == null ? 0 : 1;
		if (bestPath == null || layerZ > bestLayerZ ||
			(layerZ == bestLayerZ && (layerKind > bestLayerKind ||
				(layerKind == bestLayerKind && (ownerOrder > bestLayerOrder ||
					(ownerOrder == bestLayerOrder && candidateOrder > bestCandidateOrder)))))) {
			bestPath = path;
			bestLayerZ = layerZ;
			bestLayerKind = layerKind;
			bestLayerOrder = ownerOrder;
			bestCandidateOrder = candidateOrder;
		}
	}

	static inline function inside(rect:Rect, x:Float, y:Float):Bool
		return x >= rect.x && y >= rect.y && x <= rect.x + rect.width && y <= rect.y + rect.height;
}
