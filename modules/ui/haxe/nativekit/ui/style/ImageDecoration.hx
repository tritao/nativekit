package nativekit.ui.style;

import Canvas;
import Image;
import Rect;
import ResolvedLayoutItem;

/** Fills the resolved node bounds with an image. */
class ImageDecoration implements Decoration {
	public final image:Image;

	public function new(image:Image) {
		if (image == null || image.isDisposed())
			throw "Image decorations require a live image";
		this.image = image;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void
		canvas.drawImage(image, new Rect(0.0, 0.0, geometry.width, geometry.height));
}
