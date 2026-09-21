package nativekit.ui.core;

/** Stable invocation data shared by menus, shortcuts, palettes, and scripts. */
class CommandContext {
	public final document:Null<EditorDocument>;
	public final selection:Array<String>;
	public final viewportId:Null<String>;
	public final parameters:CommandParameters;
	public final source:Null<String>;

	public function new(?document:EditorDocument, ?selection:Array<String>,
			?viewportId:String, ?parameters:CommandParameters, ?source:String) {
		this.document = document;
		this.selection = selection == null ? [] : selection.copy();
		this.viewportId = viewportId;
		this.parameters = parameters == null ? new CommandParameters() : parameters;
		this.source = source;
	}

	public var hasSelection(get, never):Bool;
	inline function get_hasSelection():Bool
		return selection.length > 0;

	public function hasSelected(id:String):Bool
		return id != null && selection.indexOf(id) >= 0;
}
