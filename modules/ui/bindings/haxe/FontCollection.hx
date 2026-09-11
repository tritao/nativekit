/** Collection of fonts used to create text layouts. */
import FontFamily;
import haxe.io.Bytes;

class FontCollection extends NativeKitUIResource {
	private function new(value:nkui_resource)
		super(value);

	public static function create():FontCollection {
		var made = NativeKitUI.nkui_font_collection_create();
		UiResult.check(made.status, "fonts.create");
		return new FontCollection(made.out_fonts);
	}

	public function add(path:String, family:FontFamily = FontFamily.Default):Void
		UiResult.check(NativeKitUI.nkui_font_collection_add(nativeHandle(), path, cast family), "fonts.add");

	/** Adds font bytes, which is useful when a browser asset has been fetched into memory. */
	public function addData(name:String, data:Bytes, family:FontFamily = FontFamily.Default):Void
		UiResult.check(NativeKitUI.nkui_font_collection_add_data(nativeHandle(), name, data,
			cast family), "fonts.addData");

	/** Adds platform-provided script and emoji fallback fonts when available. */
	public function addSystemFallbacks():Void
		UiResult.check(NativeKitUI.nkui_font_collection_add_system_fallbacks(nativeHandle()),
			"fonts.addSystemFallbacks");
}
