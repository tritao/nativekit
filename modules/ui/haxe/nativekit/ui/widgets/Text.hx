package nativekit.ui.widgets;

import Color;
import LayoutVisualKind;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Text view backed by the NativeUI/Skribidi text layout primitive. */
class Text implements View {
	public var value:String;
	public final style:LayoutStyle;
	public var color:Null<Color>;
	public final textStyle:Null<TextStyleOverride>;

	public function new(value:String, ?style:LayoutStyle, ?color:Color,
			?textStyle:TextStyleOverride) {
		this.value = value == null ? "" : value;
		this.style = style == null ? new LayoutStyle() : style;
		this.color = color;
		this.textStyle = textStyle;
	}

	public function build(context:BuildContext):RenderNode {
		var node = new RenderNode(context.id("text"), LayoutVisualKind.Text, style);
		node.layout.text = value;
		var resolved = context.resolveTextStyle(textStyle);
		if (color != null)
			resolved = resolved.withTextColor(color);
		node.applyTextStyle(resolved);
		node.semantics = new Semantics(AccessibilityRole.Text, value);
		return node;
	}
}
