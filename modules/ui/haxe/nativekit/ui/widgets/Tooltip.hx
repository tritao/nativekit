package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;

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
			tooltipStyle.padding = new Insets(6.0, 6.0, 4.0, 4.0);
			tooltipStyle.background = Color.rgba(0.08, 0.09, 0.11, 0.96);
			var tooltip = new AnonymousTooltipContent(content, tooltipStyle);
			var stack = new Stack("layers", [
				new StackChild("anchor", anchor, 0.0, 0.0, 0),
				new StackChild("tooltip", tooltip, x, y, 10)
			]);
			var root = context.withScope(new Key("layers"), function() return stack.build(context));
			var anchorNode = root.children[0];
			var tooltipNode = root.children[1];
			tooltipNode.layout.style.visible = cast state.value;
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
		var node = new RenderNode(context.id("tooltip"), LayoutVisualKind.Box, style);
		var child = context.withScope(new Key("content"), function() return content.build(context));
		node.add(child);
		return node;
	}
}
