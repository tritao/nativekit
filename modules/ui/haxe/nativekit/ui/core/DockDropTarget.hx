package nativekit.ui.core;

import Point;

/** Resolved render geometry that accepts a dock drop for one panel group. */
class DockDropTarget {
	public final targetPanelId:String;
	public final node:RenderNode;

	public function new(targetPanelId:String, node:RenderNode) {
		if (targetPanelId == null || targetPanelId.length == 0 || node == null)
			throw "Dock drop targets require a target panel and render node";
		this.targetPanelId = targetPanelId;
		this.node = node;
	}

	public function contains(x:Float, y:Float):Bool
		return node.resolved != null && node.containsGlobalPoint(new Point(x, y));

	public function zoneAt(x:Float, y:Float):Null<DockDropZone> {
		if (!contains(x, y))
			return null;
		var bounds = node.globalBounds();
		if (bounds.width <= 0.0 || bounds.height <= 0.0)
			return DockDropZone.Center;
		var normalizedX = (x - bounds.x) / bounds.width;
		var normalizedY = (y - bounds.y) / bounds.height;
		var edge = 0.25;
		var result = DockDropZone.Center;
		var distance = 1.0;
		if (normalizedX < edge && normalizedX < distance) {
			result = DockDropZone.Left;
			distance = normalizedX;
		}
		if (normalizedX > 1.0 - edge && 1.0 - normalizedX < distance) {
			result = DockDropZone.Right;
			distance = 1.0 - normalizedX;
		}
		if (normalizedY < edge && normalizedY < distance) {
			result = DockDropZone.Top;
			distance = normalizedY;
		}
		if (normalizedY > 1.0 - edge && 1.0 - normalizedY < distance)
			result = DockDropZone.Bottom;
		return result;
	}
}
