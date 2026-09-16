package nativekit.ui.widgets;

import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;

/** Horizontal composition over a generic box render node. */
class Row implements View {
	final key:Key;
	final children:Array<KeyedView>;
	public final style:LayoutStyle;

	public function new(key:String, children:Array<KeyedView>, ?style:LayoutStyle) {
		this.key = new Key(key);
		this.children = children == null ? [] : children;
		this.style = style == null ? new LayoutStyle() : style.copy();
		this.style.direction = LayoutDirection.LeftToRight;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var id = context.id("row");
			var computed = context.resolveStyle(new StyleTarget("row", key.value, key.value,
				null, ["row"], context.interactionStates.get(id)), style);
			var node = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("row", key.value, key.value, null, ["row"]);
			node.states = context.interactionStates.get(id);
			node.computedStyle = computed;
			for (child in children)
				node.add(context.withStyleParent(computed, function() return child.build(context)));
			return node;
		});
	}
}
