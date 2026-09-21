package nativekit.ui.core;

import Point;

/** Header geometry used to reorder tabs without materializing another dock. */
class DockTabDropTarget {
	public final targetPanelId:String;
	public final node:RenderNode;

	public function new(targetPanelId:String, node:RenderNode) {
		if (targetPanelId == null || targetPanelId.length == 0 || node == null)
			throw "Tab drop targets require a target panel and render node";
		this.targetPanelId = targetPanelId;
		this.node = node;
	}

	public function contains(x:Float, y:Float):Bool
		return node.resolved != null && node.containsGlobalPoint(new Point(x, y));

	public function zoneAt(x:Float, y:Float):Null<DockDropZone> {
		if (!contains(x, y))
			return null;
		var bounds = node.globalBounds();
		if (bounds.width <= 0.0)
			return DockDropZone.TabAfter;
		return x < bounds.x + bounds.width * 0.5
			? DockDropZone.TabBefore : DockDropZone.TabAfter;
	}
}
