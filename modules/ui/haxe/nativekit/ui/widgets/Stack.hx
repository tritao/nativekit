package nativekit.ui.widgets;

import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;

/** Parent-relative positioned composition with native rendering and Haxe hit testing. */
class Stack implements View {
	final key:Key;
	final layers:Array<StackChild>;
	public final style:LayoutStyle;

	public function new(key:String, layers:Array<StackChild>, ?style:LayoutStyle) {
		this.key = new Key(key);
		this.layers = layers == null ? [] : layers.copy();
		this.style = style == null ? defaultStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var id = context.id("stack");
			var computed = context.resolveStyle(new StyleTarget("stack", key.value, key.value,
				null, ["stack"], context.interactionStates.get(id)), style);
			var root = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			root.setStyleIdentity("stack", key.value, key.value, null, ["stack"]);
			root.states = context.interactionStates.get(id);
			root.computedStyle = computed;
			for (layer in layers) {
				var child = context.withStyleParent(computed, function() return
					context.withScope(new Key(layer.key), function() {
					var node = layer.view.build(context);
					node.layout.style.positioning = LayoutPositioning.Absolute;
					node.layout.style.positionX = layer.x;
					node.layout.style.positionY = layer.y;
					node.layout.style.zIndex = layer.zIndex;
					node.layout.style.clipToParent = layer.clipToParent;
					if (layer.width != null)
						node.layout.style.width = cast layer.width;
					if (layer.height != null)
						node.layout.style.height = cast layer.height;
					return node;
					}));
				root.add(child);
			}
			return root;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		return result;
	}
}
