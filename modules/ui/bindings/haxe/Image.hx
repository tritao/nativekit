import haxe.io.Bytes;
import ImageFormat;

/** An immutable image resource uploaded to the UI renderer. */
class Image extends NativeKitUIResource {
	public final width:Int;
	public final height:Int;
	public final format:ImageFormat;

	private function new(value:nkui_resource, width:Int, height:Int, format:ImageFormat) {
		super(value);
		this.width = width;
		this.height = height;
		this.format = format;
	}

	public static function create(width:Int, height:Int, format:ImageFormat, pixels:Bytes):Image {
		if (width <= 0 || height <= 0)
			throw "Image dimensions must be positive";
		var bytesPerPixel = format == ImageFormat.R8 ? 1 : format == ImageFormat.RGBA8 ? 4 : 0;
		if (bytesPerPixel == 0 || pixels == null || pixels.length != width * height * bytesPerPixel)
			throw "Image pixel data does not match its dimensions and format";
		var made = NativeKitUI.nkui_image_create(width, height, cast format, pixels);
		UiResult.check(made.status, "image.create");
		return new Image(made.out_image, width, height, format);
	}
}
