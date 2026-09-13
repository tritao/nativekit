package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

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
			var node = new RenderNode(context.id("sized-box"), LayoutVisualKind.Box, style);
			var content = context.withScope(new Key("content"), function() return child.build(context));
			node.add(content);
			return node;
		});
	}
}
