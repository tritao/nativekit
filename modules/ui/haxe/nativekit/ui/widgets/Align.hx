package nativekit.ui.widgets;

import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Places a child within its allocated box using NativeUI's axis alignment. */
class Align implements View {
	final key:Key;
	final child:View;
	public final style:LayoutStyle;

	public function new(key:String, child:View, alignmentX:LayoutAlignmentX = LayoutAlignmentX.Center,
			alignmentY:LayoutAlignmentY = LayoutAlignmentY.Center, ?style:LayoutStyle) {
		if (child == null)
			throw "Align requires a child view";
		this.key = new Key(key);
		this.child = child;
		this.style = style == null ? defaultStyle() : style.copy();
		this.style.childAlignX = alignmentX;
		this.style.childAlignY = alignmentY;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var node = new RenderNode(context.id("align"), LayoutVisualKind.Box, style);
			var content = context.withScope(new Key("content"), function() return child.build(context));
			node.add(content);
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		return result;
	}
}
