package nativekit.ui.widgets;

/** Bulk metadata for one TreeView root. */
class TreeRootMetadata {
	public final key:String;
	public final initiallyExpanded:Bool;

	public function new(key:String, initiallyExpanded:Bool = false) {
		this.key = key;
		this.initiallyExpanded = initiallyExpanded;
	}
}
