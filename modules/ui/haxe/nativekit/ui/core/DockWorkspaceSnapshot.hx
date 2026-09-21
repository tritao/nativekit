package nativekit.ui.core;

/** Versioned value snapshot suitable for project or user layout persistence. */
class DockWorkspaceSnapshot {
	public static inline var CurrentVersion:Int = 1;
	public final version:Int;
	public final root:DockNode;
	public final activePanelId:Null<String>;

	public function new(root:DockNode, activePanelId:Null<String>,
		version:Int = CurrentVersion) {
		if (root == null || version <= 0)
			throw "Dock workspace snapshots require a root and positive version";
		this.version = version;
		this.root = DockNodeTools.clone(root);
		this.activePanelId = activePanelId;
	}
}
