package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/** Empty layout node that can consume available space on either axis. */
class Spacer implements View {
	final key:Key;
	public final style:LayoutStyle;

	public function new(key:String, ?width:LayoutAxis, ?height:LayoutAxis) {
		this.key = new Key(key);
		style = new LayoutStyle();
		style.width = width == null ? LayoutAxis.grow() : width;
		style.height = height == null ? LayoutAxis.grow() : height;
	}

	public function build(context:BuildContext):RenderNode
		return context.withScope(key, function() {
			return new RenderNode(context.id("spacer"), LayoutVisualKind.Box, style);
		});
}
