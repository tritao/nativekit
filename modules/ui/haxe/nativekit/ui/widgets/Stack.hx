package nativekit.ui.widgets;

import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

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
			var root = new RenderNode(context.id("stack"), LayoutVisualKind.Box, style.copy());
			for (layer in layers) {
				var child = context.withScope(new Key(layer.key), function() {
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
				});
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
