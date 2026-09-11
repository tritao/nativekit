/** Collection of fonts used to create text layouts. */
import FontFamily;

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
}
