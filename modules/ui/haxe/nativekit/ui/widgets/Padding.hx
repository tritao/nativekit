package nativekit.ui.widgets;

import Insets;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

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
			var node = new RenderNode(context.id("padding"), LayoutVisualKind.Box, style);
			var content = context.withScope(new Key("content"), function() return child.build(context));
			node.add(content);
			return node;
		});
	}
}
