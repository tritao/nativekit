package nativekit.ui.style;

import Canvas;
import Image;
import Rect;
import ResolvedLayoutItem;

/** Fills the resolved node bounds with an image. */
class ImageDecoration extends Decoration {
	public final image:Image;

	public function new(image:Image) {
		super(DecorationKind.Image);
		if (image == null || image.isDisposed())
			throw "Image decorations require a live image";
		this.image = image;
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void
		canvas.drawImage(image, new Rect(0.0, 0.0, geometry.width, geometry.height));

	override public function copy():Decoration
		return new ImageDecoration(image);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		return image == cast(other, ImageDecoration).image;
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration
		return Decoration.discrete(this, other, amount);

	override public function describe():String
		return 'image(${image.identity},${image.width}x${image.height})';
}
