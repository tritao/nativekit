package nativekit.ui.widgets;

import Color;
import LayoutVisualKind;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Text view backed by the NativeUI/Skribidi text layout primitive. */
class Text implements View {
	public var value:String;
	public final style:LayoutStyle;
	public var color:Color;

	public function new(value:String, ?style:LayoutStyle, ?color:Color) {
		this.value = value == null ? "" : value;
		this.style = style == null ? new LayoutStyle() : style;
		this.color = color == null ? Color.rgba(1.0, 1.0, 1.0, 1.0) : color;
	}

	public function build(context:BuildContext):RenderNode {
		var node = new RenderNode(context.id("text"), LayoutVisualKind.Text, style);
		node.layout.text = value;
		node.layout.textColor = color;
		node.semantics = new Semantics(AccessibilityRole.Text, value);
		return node;
	}
}
