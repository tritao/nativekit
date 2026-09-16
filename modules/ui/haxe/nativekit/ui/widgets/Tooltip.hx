package nativekit.ui.widgets;

import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;

/** Hover-triggered tooltip layered above its anchor with persistent Haxe state. */
class Tooltip implements View {
	final key:Key;
	final anchor:View;
	final content:View;
	public final x:Float;
	public final y:Float;

	public function new(key:String, anchor:View, content:View, x:Float = 0.0, y:Float = -28.0) {
		if (anchor == null || content == null)
			throw "Tooltip requires an anchor and content view";
		this.key = new Key(key);
		this.anchor = anchor;
		this.content = content;
		this.x = x;
		this.y = y;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var state:State<Bool> = context.state(context.id("visible"), false);
			var tooltipStyle = new LayoutStyle();
			tooltipStyle.positioning = LayoutPositioning.Absolute;
			tooltipStyle.positionX = x;
			tooltipStyle.positionY = y;
			tooltipStyle.zIndex = 10;
			tooltipStyle.clipToParent = false;
			tooltipStyle.visible = cast state.value;
			var tooltip = new AnonymousTooltipContent(content, tooltipStyle);
			var rootStyle = new LayoutStyle();
			rootStyle.width = LayoutAxis.fit();
			rootStyle.height = LayoutAxis.fit();
			rootStyle.clipToParent = false;
			var root = context.withScope(new Key("layers"), function() {
				var rootId = context.id("stack");
				var rootComputed = context.resolveStyle(
					new StyleTarget("tooltip-layer", key.value, key.value, null, ["tooltip-layer"],
						context.interactionStates.get(rootId)), rootStyle);
				var result = new RenderNode(rootId, LayoutVisualKind.Box, rootComputed.toLayoutStyle());
				result.setStyleIdentity("tooltip-layer", key.value, key.value, null, ["tooltip-layer"]);
				result.states = context.interactionStates.get(rootId);
				result.computedStyle = rootComputed;
				var anchorNode = context.withStyleParent(rootComputed, function() return
					context.withScope(new Key("anchor"), function() return anchor.build(context)));
				var tooltipNode = context.withStyleParent(rootComputed, function() return
					context.withScope(new Key("tooltip"), function() return tooltip.build(context)));
				result.add(anchorNode);
				result.add(tooltipNode);
				return result;
			});
			var anchorNode = root.children[0];
			var tooltipNode = root.children[1];
			tooltipNode.layout.style.visible = state.value;
			anchorNode.on(UiEventKind.HoverEnter, function(_) { state.update(true); });
			anchorNode.on(UiEventKind.HoverLeave, function(_) { state.update(false); });
			tooltipNode.on(UiEventKind.HoverEnter, function(_) { state.update(true); });
			tooltipNode.on(UiEventKind.HoverLeave, function(_) { state.update(false); });
			root.hitTestSelf = false;
			return root;
		});
	}
}

private class AnonymousTooltipContent implements View {
	final content:View;
	final style:LayoutStyle;

	public function new(content:View, style:LayoutStyle) {
		this.content = content;
		this.style = style;
	}

	public function build(context:BuildContext):RenderNode {
		var nodeId = context.id("tooltip");
		var computed = context.resolveStyle(
			new StyleTarget("tooltip", "tooltip", "tooltip", null, ["tooltip"],
				context.interactionStates.get(nodeId)), style);
		var node = new RenderNode(nodeId, LayoutVisualKind.Box, computed.toLayoutStyle());
		node.setStyleIdentity("tooltip", "tooltip", "tooltip", null, ["tooltip"]);
		node.states = context.interactionStates.get(nodeId);
		node.computedStyle = computed;
		var child = context.withStyleParent(computed, function() return
			context.withScope(new Key("content"), function() return content.build(context)));
		node.add(child);
		return node;
	}
}
