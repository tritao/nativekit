package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutStyle;
import Rect;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.DockDropZone;
import nativekit.ui.core.DockNode;
import nativekit.ui.core.DockDropTarget;
import nativekit.ui.core.DockPanelDescriptor;
import nativekit.ui.core.DockSplitAxis;
import nativekit.ui.core.DockWorkspaceInteraction;
import nativekit.ui.core.DockWorkspaceModel;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Renders a DockWorkspaceModel using split panes, tab groups, and lazy panels. */
class DockWorkspace implements View {
	public final key:String;
	public final model:DockWorkspaceModel;
	public final interaction:DockWorkspaceInteraction;
	public final style:LayoutStyle;
	var invalidate:Null<Void->Void>;
	var subscribed:Bool;

	public function new(key:String, model:DockWorkspaceModel, ?style:LayoutStyle,
			?interaction:DockWorkspaceInteraction) {
		if (key == null || key.length == 0 || model == null)
			throw "Dock workspaces require a stable key and model";
		this.key = key;
		this.model = model;
		this.interaction = interaction == null ? new DockWorkspaceInteraction(model) : interaction;
		this.style = style == null ? defaultStyle() : style.copy();
		invalidate = null;
		subscribed = false;
	}

	public function build(context:BuildContext):RenderNode {
		if (!subscribed) {
			model.listen(function() {
				if (invalidate != null)
					invalidate();
			});
			interaction.listen(function() {
				if (invalidate != null)
					invalidate();
			});
			subscribed = true;
		}
		invalidate = function() context.commands.refresh();
		interaction.beginFrame();
		var content = buildNode(model.root, context, [], "layout");
		var layout = new SizedBox("layout", content, LayoutAxis.grow(), LayoutAxis.grow());
		return new Column(key, [new KeyedView("content", layout)], style).build(context);
	}

	function buildNode(node:DockNode, context:BuildContext, path:Array<Int>, nodeKey:String):View {
		if (node == null)
			return new Text("No dock layout");
		switch (node) {
			case DockNode.Empty: return new Text("No panels");
			case DockNode.Panel(panelId): return targetView(panelId, panelView(panelId));
			case DockNode.Tabs(panelIds, activePanelId):
				var targetPanelId = activePanelId == null && panelIds != null && panelIds.length > 0
					? panelIds[0] : activePanelId;
				return targetPanelId == null ? buildTabs(panelIds, activePanelId, context, nodeKey) :
					targetView(targetPanelId, buildTabs(panelIds, activePanelId, context, nodeKey));
			case DockNode.Split(axis, ratio, first, second):
				return buildSplit(axis, ratio, first, second, context, path, nodeKey);
		}
	}

	function buildTabs(panelIds:Array<String>, activePanelId:String,
		context:BuildContext, nodeKey:String):View {
		var items:Array<TabItem> = [];
		if (panelIds != null)
			for (panelId in panelIds) {
				var descriptor = model.get(panelId);
				if (descriptor != null)
					items.push(new TabItem(panelId, descriptor.title, panelView(panelId),
						descriptor.enabled));
			}
		var tabsStyle = new LayoutStyle();
		tabsStyle.width = LayoutAxis.grow();
		tabsStyle.height = LayoutAxis.grow();
		return new Tabs(nodeKey, items, activePanelId, function(next) {
			model.activate(next);
		}, tabsStyle,
			function(panelId, event) interaction.beginTabDrag(panelId, event.pointerId, event.x, event.y),
			function(panelId, event) interaction.moveTabDrag(panelId, event.pointerId, event.x, event.y),
			function(panelId, event) interaction.endTabDrag(panelId, event.pointerId, event.x, event.y),
			function(panelId, event) interaction.cancelTabDrag(panelId, event.pointerId));
	}

	function buildSplit(axis:DockSplitAxis, ratio:Float, first:DockNode, second:DockNode,
		context:BuildContext, path:Array<Int>, nodeKey:String):View {
		var firstPath = path.copy();
		firstPath.push(0);
		var secondPath = path.copy();
		secondPath.push(1);
		var horizontal = axis == DockSplitAxis.Horizontal;
		var available = horizontal ? context.viewportWidth : context.viewportHeight;
		if (available <= 0.0)
			available = 1000.0;
		var minimum = 120.0;
		var divider = 8.0;
		var maximum = available - minimum - divider;
		if (maximum < minimum)
			maximum = minimum;
		var options = new SplitViewOptions();
		options.orientation = horizontal ? SplitOrientation.Horizontal : SplitOrientation.Vertical;
		options.resizableSide = SplitSide.Leading;
		options.extent = clamp(ratio * available, minimum, maximum);
		options.minimumExtent = minimum;
		options.maximumExtent = maximum;
		options.dividerExtent = divider;
		options.onResize = function(next) {
			model.setSplitRatio(path, next / available);
		};
		return new SplitView(nodeKey, buildNode(first, context, firstPath, nodeKey + ":first"),
			buildNode(second, context, secondPath, nodeKey + ":second"), options);
	}

	function panelView(panelId:String):View {
		var descriptor = model.get(panelId);
		return descriptor == null ? new Text("Missing panel: " + panelId) :
			new DockPanelView(descriptor);
	}

	function targetView(targetPanelId:String, child:View):View
		return new DockDropTargetView(child, interaction, targetPanelId);

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		return result;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;
}

private class DockPanelView implements View {
	final descriptor:DockPanelDescriptor;

	public function new(descriptor:DockPanelDescriptor)
		this.descriptor = descriptor;

	public function build(context:BuildContext):RenderNode {
		var content = descriptor.build(context);
		if (content == null)
			content = new Text("Panel returned no content: " + descriptor.id);
		return context.withScope(new Key("panel:" + descriptor.id), function() {
			return content.build(context);
		});
	}
}

private class DockDropTargetView implements View {
	final child:View;
	final interaction:DockWorkspaceInteraction;
	final targetPanelId:String;

	public function new(child:View, interaction:DockWorkspaceInteraction,
			targetPanelId:String) {
		if (child == null || interaction == null || targetPanelId == null)
			throw "Dock drop target views require a child, interaction, and target";
		this.child = child;
		this.interaction = interaction;
		this.targetPanelId = targetPanelId;
	}

	public function build(context:BuildContext):RenderNode {
		var node = child.build(context);
		interaction.registerTarget(new DockDropTarget(targetPanelId, node));
		var activePreview = interaction.preview;
		if (activePreview != null && activePreview.targetPanelId == targetPanelId)
			node.onPaint(function(canvas, geometry) {
				var preview = interaction.preview;
				if (preview == null || preview.targetPanelId != targetPanelId)
					return;
				canvas.fillRectIfPositive(previewRect(preview.zone, geometry.width, geometry.height),
					Color.rgba(0.18, 0.52, 0.95, 0.22));
			});
		return node;
	}

	static function previewRect(zone:DockDropZone, width:Float, height:Float):Rect {
		return switch (zone) {
			case DockDropZone.Center: new Rect(width * 0.2, height * 0.2, width * 0.6, height * 0.6);
			case DockDropZone.Left: new Rect(0.0, 0.0, width * 0.25, height);
			case DockDropZone.Right: new Rect(width * 0.75, 0.0, width * 0.25, height);
			case DockDropZone.Top: new Rect(0.0, 0.0, width, height * 0.25);
			case DockDropZone.Bottom: new Rect(0.0, height * 0.75, width, height * 0.25);
		};
	}
}
