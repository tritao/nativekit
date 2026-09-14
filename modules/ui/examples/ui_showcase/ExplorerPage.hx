import nativekit.ui.widgets.KeyedView;

/** Searchable catalog metadata and builder for one Explorer page. */
class ExplorerPage {
	public final id:String;
	public final title:String;
	public final group:String;
	public final description:String;
	public final keywords:Array<String>;
	public final builder:UiExplorer -> Array<KeyedView> -> Void;

	public function new(id:String, title:String, group:String, description:String,
			keywords:Array<String>, builder:UiExplorer -> Array<KeyedView> -> Void) {
		this.id = id;
		this.title = title;
		this.group = group;
		this.description = description;
		this.keywords = keywords;
		this.builder = builder;
	}
}
