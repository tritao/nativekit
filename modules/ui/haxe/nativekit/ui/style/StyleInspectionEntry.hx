package nativekit.ui.style;

/** Inspectable computed property entry kept as a standalone Haxe module. */
class StyleInspectionEntry {
	public final name:String;
	public final value:Dynamic;
	public final source:Null<StyleSource>;

	public function new(name:String, value:Dynamic, source:Null<StyleSource>) {
		this.name = name;
		this.value = value;
		this.source = source;
	}
}
