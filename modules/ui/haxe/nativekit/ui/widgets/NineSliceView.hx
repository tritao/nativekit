package nativekit.ui.widgets;

import Image;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.NineSliceDecoration;
import nativekit.ui.style.DecorationChain;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleTarget;

/** Accessible image view that preserves four edge insets while stretching its center. */
class NineSliceView implements View {
	public final key:String;
	public final image:Image;
	public final label:Null<String>;
	public final style:LayoutStyle;
	public final left:Float;
	public final top:Float;
	public final right:Float;
	public final bottom:Float;

	public function new(key:String, image:Image, left:Float, top:Float, right:Float,
			bottom:Float, ?label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || image == null || image.isDisposed() ||
			left < 0.0 || top < 0.0 || right < 0.0 || bottom < 0.0)
			throw "Nine-slice views require a stable key, live image, and non-negative insets";
		this.key = key;
		this.image = image;
		this.left = left;
		this.top = top;
		this.right = right;
		this.bottom = bottom;
		this.label = label;
		this.style = style == null ? new LayoutStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("nine-slice");
			var computed = context.resolveStyle(new StyleTarget("nine-slice", key, key,
				null, ["nine-slice"], context.interactionStates.get(nodeId)), style);
			var resolvedDecorations = computed.get(StyleProperty.Decorations);
			var decorationSource = computed.source(StyleProperty.Decorations);
			if ((resolvedDecorations == null || resolvedDecorations.decorations.length == 0) &&
				decorationSource != null && decorationSource.stylesheet == "framework" &&
				decorationSource.selector == "default")
				computed.set(StyleProperty.Decorations, DecorationChain.of([
					new NineSliceDecoration(image, left, top, right, bottom)
				]), new StyleSource("framework", "nine-slice", -1, "default"));
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("nine-slice", key, key, null, ["nine-slice"]);
			node.states = context.interactionStates.get(nodeId);
			node.computedStyle = computed;
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Image, label);
			return node;
		});
	}
}
