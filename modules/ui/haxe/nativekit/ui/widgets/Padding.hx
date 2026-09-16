package nativekit.ui.widgets;

import Insets;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;

/** Adds layout-engine padding around one child view. */
class Padding implements View {
	final key:Key;
	final child:View;
	final insets:Insets;
	public final style:LayoutStyle;

	public function new(key:String, child:View, ?insets:Insets, ?style:LayoutStyle) {
		if (child == null)
			throw "Padding requires a child view";
		this.key = new Key(key);
		this.child = child;
		this.insets = insets == null ? new Insets(0.0, 0.0, 0.0, 0.0) :
			new Insets(insets.left, insets.top, insets.right, insets.bottom);
		this.style = style == null ? new LayoutStyle() : style.copy();
		this.style.padding = new Insets(this.insets.left, this.insets.top,
			this.insets.right, this.insets.bottom);
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var id = context.id("padding");
			var computed = context.resolveStyle(new StyleTarget("padding", key.value, key.value,
				null, ["padding"], context.interactionStates.get(id)), style);
			var node = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("padding", key.value, key.value, null, ["padding"]);
			node.states = context.interactionStates.get(id);
			node.computedStyle = computed;
			var content = context.withStyleParent(computed, function() {
				return context.withScope(new Key("content"), function() return child.build(context));
			});
			node.add(content);
			return node;
		});
	}
}
