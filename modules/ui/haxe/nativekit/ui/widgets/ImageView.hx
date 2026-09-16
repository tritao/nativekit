package nativekit.ui.widgets;

import Canvas;
import Image;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/** Accessible image view that records its draw in the node's custom display list. */
class ImageView implements View {
	public final key:String;
	public final image:Image;
	public final label:Null<String>;
	public final style:LayoutStyle;
	public var fit:ImageFit;

	public function new(key:String, image:Image, ?label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || image == null || image.isDisposed())
			throw "Image views require a stable key and live image";
		this.key = key;
		this.image = image;
		this.label = label;
		fit = ImageFit.Stretch;
		if (style == null) {
			this.style = new LayoutStyle();
			this.style.width = LayoutAxis.fixed(image.width);
			this.style.height = LayoutAxis.fixed(image.height);
		} else
			this.style = style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("image");
			var computed = context.styleResolver.resolve(
				new StyleTarget("image", key, key, null, ["image"],
					context.interactionStates.get(nodeId)),
				context.inheritedStyle, context.theme.styles, context.styleSheet, style, context.environment);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("image", key, key, null, ["image"]);
			node.states = context.interactionStates.get(nodeId);
			node.computedStyle = computed;
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Image, label);
			node.onPaint(function(canvas:Canvas, geometry:ResolvedLayoutItem) {
				if (image.isDisposed() || geometry.width <= 0.0 || geometry.height <= 0.0)
					return;
				var scale = switch fit {
					case ImageFit.Contain: Math.min(geometry.width / image.width, geometry.height / image.height);
					case ImageFit.Cover: Math.max(geometry.width / image.width, geometry.height / image.height);
					case ImageFit.None: 1.0;
					default: 0.0;
				};
				if (fit == ImageFit.Stretch) {
					canvas.drawImage(image, new Rect(0.0, 0.0, geometry.width, geometry.height));
					return;
				}
				var width = image.width * scale;
				var height = image.height * scale;
				var destination = new Rect((geometry.width - width) * 0.5,
					(geometry.height - height) * 0.5, width, height);
				canvas.withClip(new Rect(0.0, 0.0, geometry.width, geometry.height),
					function(target) target.drawImage(image, destination));
			});
			return node;
		});
	}
}
