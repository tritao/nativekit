package nativekit.ui.widgets;

import nativekit.ui.core.View;

/** One stable tab key, accessible label, and lazily built page. */
class TabItem {
	public final key:String;
	public final label:String;
	public final content:View;
	public final enabled:Bool;

	public function new(key:String, label:String, content:View, enabled:Bool = true) {
		if (key == null || key.length == 0 || content == null)
			throw "Tabs require stable keys and content views";
		this.key = key;
		this.label = label == null ? "" : label;
		this.content = content;
		this.enabled = enabled;
	}
}
