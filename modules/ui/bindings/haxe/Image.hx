import haxe.io.Bytes;
import ImageFormat;

/** An immutable image resource uploaded to the UI renderer. */
class Image extends NativeKitUIResource {
	public final width:Int;
	public final height:Int;
	public final format:ImageFormat;
	public final filter:ImageFilter;

	private function new(value:nkui_resource, width:Int, height:Int, format:ImageFormat,
			filter:ImageFilter) {
		super(value);
		this.width = width;
		this.height = height;
		this.format = format;
		this.filter = filter;
	}

	public static function create(width:Int, height:Int, format:ImageFormat, pixels:Bytes,
			filter:ImageFilter = ImageFilter.Linear):Image {
		if (width <= 0 || height <= 0)
			throw "Image dimensions must be positive";
		var bytesPerPixel = format == ImageFormat.R8 ? 1 : format == ImageFormat.RGBA8 ? 4 : 0;
		if (bytesPerPixel == 0 || pixels == null || pixels.length != width * height * bytesPerPixel)
			throw "Image pixel data does not match its dimensions and format";
		var made = NativeKitUI.nkui_image_create_filtered(width, height, cast format, pixels,
			cast filter);
		UiResult.check(made.status, "image.create");
		return new Image(made.out_image, width, height, format, filter);
	}

	/** Synchronously decodes an image file. Use an application worker for blocking I/O. */
	public static function loadFile(path:String,
			filter:ImageFilter = ImageFilter.Linear):Image {
		if (path == null || path.length == 0)
			throw "Image path must not be empty";
		var made = NativeKitUI.nkui_image_load_file(path, cast filter);
		UiResult.check(made.status, "image.loadFile");
		return new Image(made.out_image, made.out_width, made.out_height,
			ImageFormat.RGBA8, filter);
	}
}
