package components;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Haxeon-owned layout placeholder synchronized to a native child WebView. */
class WebViewSlot implements View {
	public static inline var SemanticLabel = "Haxeon WebView viewport";

	public final key:String;
	public final style:LayoutStyle;

	public function new(key:String, ?style:LayoutStyle) {
		this.key = key;
		this.style = style == null ? defaultStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("webview-slot"), LayoutVisualKind.Box, style);
			node.semantics = new Semantics(AccessibilityRole.Group, SemanticLabel);
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(430.0);
		result.clipToParent = true;
		return result;
	}
}
