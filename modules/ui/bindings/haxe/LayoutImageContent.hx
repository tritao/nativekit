/** Immutable intrinsic provider for an uploaded Image resource. */
class LayoutImageContent implements LayoutContent {
	public final image:Image;

	public function new(image:Image) {
		if (image == null || image.isDisposed())
			throw "Image content requires a live image";
		this.image = image;
	}

	public function getVersion():Int
		return 0;

	public function measure(constraints:LayoutMeasureConstraints):LayoutMeasureResult
		return new LayoutMeasureResult(image.width, image.height);
}
