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

/** Accessible image view that records its draw in the node's custom display list. */
class ImageView implements View {
	public final key:String;
	public final image:Image;
	public final label:Null<String>;
	public final style:LayoutStyle;

	public function new(key:String, image:Image, ?label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || image == null || image.isDisposed())
			throw "Image views require a stable key and live image";
		this.key = key;
		this.image = image;
		this.label = label;
		if (style == null) {
			this.style = new LayoutStyle();
			this.style.width = LayoutAxis.fixed(image.width);
			this.style.height = LayoutAxis.fixed(image.height);
		} else
			this.style = style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("image"), LayoutVisualKind.Custom, style);
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Image, label);
			node.onPaint(function(canvas:Canvas, geometry:ResolvedLayoutItem) {
				if (!image.isDisposed() && geometry.width > 0.0 && geometry.height > 0.0)
					canvas.drawImage(image, new Rect(0.0, 0.0, geometry.width, geometry.height));
			});
			return node;
		});
	}
}
