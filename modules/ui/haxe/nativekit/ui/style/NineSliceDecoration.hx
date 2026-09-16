package nativekit.ui.style;

import Canvas;
import Image;
import Rect;
import ResolvedLayoutItem;

/** Image decoration seam reserved for nine-slice metadata; currently preserves the full image. */
class NineSliceDecoration implements Decoration {
	public final image:Image;
	public final left:Float;
	public final top:Float;
	public final right:Float;
	public final bottom:Float;

	public function new(image:Image, left:Float, top:Float, right:Float, bottom:Float) {
		if (image == null || image.isDisposed() || left < 0.0 || top < 0.0 ||
			right < 0.0 || bottom < 0.0)
			throw "Nine-slice decorations require a live image and non-negative insets";
		this.image = image;
		this.left = left;
		this.top = top;
		this.right = right;
		this.bottom = bottom;
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void
		canvas.drawImage(image, new Rect(0.0, 0.0, geometry.width, geometry.height));
}
