package inspector;

import Canvas;
import LayoutAxis;
import LayoutStyle;
import Rect;
import ResolvedLayoutItem;
import UiExplorer;
import nativekit.ui.core.HitTest;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.WidgetId;
import nativekit.ui.debug.UiNodeSnapshot;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.StackChild;

/** Pointer selection, focus tracking, and clipped render-node highlighting. */
class InspectionOverlay {
	public static function addHighlight(explorer:UiExplorer,
			layers:Array<StackChild>):Void {
		var highlightId = explorer.state.inspector.hoveredNodeId != 0
			? explorer.state.inspector.hoveredNodeId : explorer.state.inspector.selectedNodeId;
		if (!explorer.state.inspector.open || highlightId == 0)
			return;
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.grow();
		var highlight = new CanvasView("inspector-highlight", function(canvas, _) {
			var root = explorer.context.root;
			var node = root == null ? null : root.find(new WidgetId(highlightId));
			drawHighlight(canvas, node == null ? null : node.resolved);
		}, style, null, false);
		layers.push(new StackChild("inspector-highlight-layer", highlight, 0.0, 0.0, 32767));
	}

	public static function attachEvents(explorer:UiExplorer):Void {
		if (explorer.context.root == null)
			return;
		var root = explorer.context.root;
		if (!explorer.state.inspector.picking) {
			explorer.state.inspector.hoveredNodeId = 0;
			return;
		}
		var hovered = explorer.context.events.hoveredId();
		explorer.state.inspector.hoveredNodeId = hovered == null ? 0 :
			inspectionTargetId(explorer, hovered);
		root.on(UiEventKind.PointerMove, function(event) {
			updateHoveredAt(explorer, event.x, event.y);
		});
		root.on(UiEventKind.HoverEnter, function(event) {
			updateHoveredAt(explorer, event.x, event.y);
		});
		root.on(UiEventKind.HoverLeave, function(event) {
			updateHoveredAt(explorer, event.x, event.y);
		});
		root.on(UiEventKind.PointerDown, function(event) {
			if (isPreviewPoint(explorer, event.x, event.y)) {
				var selected = inspectionTargetId(explorer, event.target);
				if (selected != 0) {
					explorer.state.inspector.selectedNodeId = selected;
					explorer.state.inspector.hoveredNodeId = 0;
					explorer.state.inspector.picking = false;
					event.preventDefault();
					event.stopPropagation();
				}
			}
		}, "capture");
	}

	static function updateHoveredAt(explorer:UiExplorer, x:Float, y:Float):Void {
		if (!isPreviewPoint(explorer, x, y) || explorer.context.root == null) {
			explorer.state.inspector.hoveredNodeId = 0;
			return;
		}
		var path = HitTest.path(explorer.context.root, x, y);
		explorer.state.inspector.hoveredNodeId = path.length == 0
			? 0 : inspectionPathTargetId(path);
	}

	static function inspectionTargetId(explorer:UiExplorer, id:WidgetId):Int {
		if (explorer.context.root == null)
			return 0;
		return inspectionPathTargetId(HitTest.pathTo(explorer.context.root.find(id)));
	}

	static function inspectionPathTargetId(path:Array<RenderNode>):Int {
		if (path.length == 0)
			return 0;
		var id = path[path.length - 1].id;
		var semantic:Null<WidgetId> = null;
		var index = path.length - 1;
		while (index >= 0) {
			var node = path[index];
			if (node.semantics != null && semantic == null)
				semantic = node.id;
			if (node.focusable)
				return node.id.value;
			index--;
		}
		return semantic == null ? id.value : semantic.value;
	}

	static function isPreviewPoint(explorer:UiExplorer, x:Float, y:Float):Bool {
		var sidebar = explorer.width < 760.0 ? 176.0 : 212.0;
		var inspectorWidth = explorer.state.inspector.open && explorer.width >= 880.0 ? 270.0 : 0.0;
		return y >= 66.0 && x >= sidebar && x < explorer.width - inspectorWidth;
	}

	public static function isRecordInPreview(explorer:UiExplorer,
			record:UiNodeSnapshot):Bool {
		var centerX = record.bounds.x + record.bounds.width * 0.5;
		var centerY = record.bounds.y + record.bounds.height * 0.5;
		return isPreviewPoint(explorer, centerX, centerY);
	}

	static function drawHighlight(canvas:Canvas, geometry:Null<ResolvedLayoutItem>):Void {
		if (geometry == null || geometry.width <= 0.0 || geometry.height <= 0.0)
			return;
		var left = Math.max(geometry.x, geometry.clipBounds.x);
		var top = Math.max(geometry.y, geometry.clipBounds.y);
		var right = Math.min(geometry.x + geometry.width,
			geometry.clipBounds.x + geometry.clipBounds.width);
		var bottom = Math.min(geometry.y + geometry.height,
			geometry.clipBounds.y + geometry.clipBounds.height);
		if (right <= left || bottom <= top)
			return;
		var edge = Math.min(2.0, Math.min((right - left) * 0.5, (bottom - top) * 0.5));
		var outline = UiExplorer.color(0.29, 0.92, 0.72, 0.95);
		canvas.fillRectIfPositive(new Rect(left, top, right - left, edge), outline);
		canvas.fillRectIfPositive(new Rect(left, bottom - edge, right - left, edge), outline);
		canvas.fillRectIfPositive(new Rect(left, top + edge, edge,
			Math.max(0.0, bottom - top - 2.0 * edge)), outline);
		canvas.fillRectIfPositive(new Rect(right - edge, top + edge, edge,
			Math.max(0.0, bottom - top - 2.0 * edge)), outline);
	}

	public static function findSnapshot(records:Array<UiNodeSnapshot>, id:Int):Null<UiNodeSnapshot> {
		if (id == 0)
			return null;
		for (record in records)
			if (record.id == id)
				return record;
		return null;
	}
}
