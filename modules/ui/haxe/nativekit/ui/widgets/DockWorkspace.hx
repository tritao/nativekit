package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.DockNode;
import nativekit.ui.core.DockPanelDescriptor;
import nativekit.ui.core.DockSplitAxis;
import nativekit.ui.core.DockWorkspaceModel;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Renders a DockWorkspaceModel using split panes, tab groups, and lazy panels. */
class DockWorkspace implements View {
	public final key:String;
	public final model:DockWorkspaceModel;
	public final style:LayoutStyle;
	var invalidate:Null<Void->Void>;
	var subscribed:Bool;

	public function new(key:String, model:DockWorkspaceModel, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || model == null)
			throw "Dock workspaces require a stable key and model";
		this.key = key;
		this.model = model;
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
			subscribed = true;
		}
		invalidate = function() context.commands.refresh();
		var content = buildNode(model.root, context, [], "layout");
		var layout = new SizedBox("layout", content, LayoutAxis.grow(), LayoutAxis.grow());
		return new Column(key, [new KeyedView("content", layout)], style).build(context);
	}

	function buildNode(node:DockNode, context:BuildContext, path:Array<Int>, nodeKey:String):View {
		if (node == null)
			return new Text("No dock layout");
		switch (node) {
			case DockNode.Empty: return new Text("No panels");
			case DockNode.Panel(panelId): return panelView(panelId);
			case DockNode.Tabs(panelIds, activePanelId):
				return buildTabs(panelIds, activePanelId, context, nodeKey);
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
		}, tabsStyle);
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
