package nativekit.ui.style;

import Canvas;
import Image;
import Rect;
import ResolvedLayoutItem;

/** Draws an image as nine independently clipped, stretched slices. */
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

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		if (geometry.width <= 0.0 || geometry.height <= 0.0)
			return;
		var sourceLeft = Math.min(left, image.width);
		var sourceTop = Math.min(top, image.height);
		var sourceRight = Math.min(right, image.width - sourceLeft);
		var sourceBottom = Math.min(bottom, image.height - sourceTop);
		var destinationLeft = Math.min(left, geometry.width * 0.5);
		var destinationTop = Math.min(top, geometry.height * 0.5);
		var destinationRight = Math.min(right, geometry.width - destinationLeft);
		var destinationBottom = Math.min(bottom, geometry.height - destinationTop);
		var sourceX:Array<Float> = [0.0, sourceLeft, image.width - sourceRight];
		var sourceY:Array<Float> = [0.0, sourceTop, image.height - sourceBottom];
		var sourceWidth:Array<Float> = [sourceLeft,
			image.width - sourceLeft - sourceRight, sourceRight];
		var sourceHeight:Array<Float> = [sourceTop,
			image.height - sourceTop - sourceBottom, sourceBottom];
		var destinationX:Array<Float> = [0.0, destinationLeft, geometry.width - destinationRight];
		var destinationY:Array<Float> = [0.0, destinationTop, geometry.height - destinationBottom];
		var destinationWidth:Array<Float> = [destinationLeft,
			geometry.width - destinationLeft - destinationRight, destinationRight];
		var destinationHeight:Array<Float> = [destinationTop,
			geometry.height - destinationTop - destinationBottom, destinationBottom];
		for (column in 0...3)
			for (row in 0...3) {
				var sourceWidthValue = sourceWidth[column];
				var sourceHeightValue = sourceHeight[row];
				var destinationWidthValue = destinationWidth[column];
				var destinationHeightValue = destinationHeight[row];
				if (sourceWidthValue <= 0.0 || sourceHeightValue <= 0.0 ||
					destinationWidthValue <= 0.0 || destinationHeightValue <= 0.0)
					continue;
				var sourceOriginX = sourceX[column];
				var sourceOriginY = sourceY[row];
				var destinationOriginX = destinationX[column];
				var destinationOriginY = destinationY[row];
				canvas.withClip(new Rect(destinationOriginX, destinationOriginY,
					destinationWidthValue, destinationHeightValue), function(target) {
					target.translate(destinationOriginX - sourceOriginX *
						destinationWidthValue / sourceWidthValue,
						destinationOriginY - sourceOriginY *
						destinationHeightValue / sourceHeightValue);
					target.scale(destinationWidthValue / sourceWidthValue,
						destinationHeightValue / sourceHeightValue);
					target.drawImage(image, new Rect(0.0, 0.0, image.width, image.height));
				});
			}
	}
}
