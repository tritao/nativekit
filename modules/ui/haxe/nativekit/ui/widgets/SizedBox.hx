package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;

/** Constrains a child's allocated width and height. */
class SizedBox implements View {
	final key:Key;
	final child:View;
	public final style:LayoutStyle;

	public function new(key:String, child:View, ?width:LayoutAxis, ?height:LayoutAxis) {
		if (child == null)
			throw "SizedBox requires a child view";
		this.key = new Key(key);
		this.child = child;
		style = new LayoutStyle();
		style.width = width == null ? LayoutAxis.grow() : width;
		style.height = height == null ? LayoutAxis.fit() : height;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var id = context.id("sized-box");
			var computed = context.resolveStyle(new StyleTarget("sized-box", key.value, key.value,
				null, ["sized-box"], context.interactionStates.get(id)), style);
			var node = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("sized-box", key.value, key.value, null, ["sized-box"]);
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
