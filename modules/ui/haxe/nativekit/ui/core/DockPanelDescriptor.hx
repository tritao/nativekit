package nativekit.ui.core;

typedef DockPanelBuilder = BuildContext->View;

/** Application-owned metadata and lazy content factory for one dock panel. */
class DockPanelDescriptor {
	public final id:String;
	public final title:String;
	public final build:DockPanelBuilder;
	public final closable:Bool;
	public final enabled:Bool;

	public function new(id:String, title:String, build:DockPanelBuilder,
		closable:Bool = true, enabled:Bool = true) {
		if (id == null || id.length == 0 || title == null || title.length == 0 || build == null)
			throw "Dock panels require a stable ID, title, and content builder";
		this.id = id;
		this.title = title;
		this.build = build;
		this.closable = closable;
		this.enabled = enabled;
	}
}
