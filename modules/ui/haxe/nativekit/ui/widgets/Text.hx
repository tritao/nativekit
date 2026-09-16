package nativekit.ui.widgets;

import Color;
import LayoutVisualKind;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.TextRole;

/** Text view backed by the NativeUI/Skribidi text layout primitive. */
class Text implements View {
	public var value:String;
	public final style:LayoutStyle;
	public var color:Null<Color>;
	public final textStyle:Null<TextStyleOverride>;
	public final role:TextRole;

	public function new(value:String, ?style:LayoutStyle, ?color:Color,
			?textStyle:TextStyleOverride, ?role:TextRole) {
		this.value = value == null ? "" : value;
		this.style = style == null ? new LayoutStyle() : style;
		this.color = color;
		this.textStyle = textStyle;
		this.role = role == null ? TextRole.Body : role;
	}

	public function build(context:BuildContext):RenderNode {
		var id = context.id("text");
		var computed = context.resolveStyle(new StyleTarget("text", null, null, null, ["text"],
			context.interactionStates.get(id)), style);
		if (color != null)
			computed.set(StyleProperty.TextColor, color,
				new StyleSource("local", "text", -1, "local"));
		var node = new RenderNode(id, LayoutVisualKind.Text, computed.toLayoutStyle());
		node.setStyleIdentity("text", null, null, null, ["text"]);
		node.states = context.interactionStates.get(id);
		node.computedStyle = computed;
		node.layout.text = value;
		var resolved = context.resolveTextRole(role, textStyle);
		var colorSource = computed.source(StyleProperty.TextColor);
		var fontSource = computed.source(StyleProperty.FontSize);
		var letterSource = computed.source(StyleProperty.LetterSpacing);
		var computedOverride = new TextStyleOverride(null,
			textStyle == null && fontSource != null && fontSource.layer != "framework"
				? computed.get(StyleProperty.FontSize) : null,
			textStyle == null && letterSource != null && letterSource.layer != "framework"
				? computed.get(StyleProperty.LetterSpacing) : null,
			null, null, null, null,
			color == null && colorSource != null && colorSource.layer != "framework"
				? computed.get(StyleProperty.TextColor) : null);
		resolved = resolved.merge(computedOverride);
		if (color != null)
			resolved = resolved.withTextColor(color);
		node.applyTextStyle(resolved);
		node.semantics = new Semantics(AccessibilityRole.Text, value);
		return node;
	}
}
