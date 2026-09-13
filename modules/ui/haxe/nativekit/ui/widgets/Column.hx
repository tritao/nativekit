package nativekit.ui.widgets;

import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Vertical composition over a generic box render node. */
class Column implements View {
	final key:Key;
	final children:Array<KeyedView>;
	public final style:LayoutStyle;

	public function new(key:String, children:Array<KeyedView>, ?style:LayoutStyle) {
		this.key = new Key(key);
		this.children = children == null ? [] : children;
		this.style = style == null ? new LayoutStyle() : style.copy();
		this.style.direction = LayoutDirection.TopToBottom;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var node = new RenderNode(context.id("column"), LayoutVisualKind.Box, style);
			for (child in children)
				node.add(child.build(context));
			return node;
		});
	}
}
