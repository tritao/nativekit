package nativekit.ui.widgets;

import Canvas;
import Image;
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

/** One image layer positioned by normalized view coordinates. */
class ImageLayer {
	public final image:Image;
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;
	public final opacity:Float;

	public function new(image:Image, x:Float, y:Float, width:Float, height:Float,
			opacity:Float = 1.0) {
		if (image == null || image.isDisposed() || x < 0.0 || y < 0.0 || width <= 0.0 ||
			height <= 0.0 || opacity < 0.0 || opacity > 1.0)
			throw "Image layers require a live image, positive normalized bounds, and valid opacity";
		this.image = image;
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
		this.opacity = opacity;
	}
}

/** Accessible retained composition of multiple image layers. */
class LayeredImageView implements View {
	public final key:String;
	public final layers:Array<ImageLayer>;
	public final label:Null<String>;
	public final style:LayoutStyle;

	public function new(key:String, layers:Array<ImageLayer>, ?label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || layers == null || layers.length == 0)
			throw "Layered image views require a stable key and at least one layer";
		this.key = key;
		this.layers = layers.copy();
		this.label = label;
		this.style = style == null ? new LayoutStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("layered-image"), LayoutVisualKind.Custom, style);
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Image, label);
			node.onPaint(function(canvas:Canvas, geometry:ResolvedLayoutItem) {
				for (layer in layers) {
					var draw = function(target:Canvas) {
						target.drawImage(layer.image,
							new Rect(layer.x * geometry.width, layer.y * geometry.height,
								layer.width * geometry.width, layer.height * geometry.height));
					};
					if (layer.opacity < 1.0)
						canvas.withLayer(layer.opacity, draw);
					else
						draw(canvas);
				}
			});
			return node;
		});
	}
}
