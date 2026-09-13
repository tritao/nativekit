package nativekit.ui.widgets;

/** Immutable menu command description. */
class MenuItem {
	public final key:String;
	public final label:String;
	public final onSelect:Void->Void;
	public final hasSelectHandler:Bool;
	public final enabled:Bool;

	public function new(key:String, label:String, ?onSelect:Void->Void, enabled:Bool = true) {
		if (key == null || key.length == 0)
			throw "Menu items require stable non-empty keys";
		this.key = key;
		this.label = label == null ? "" : label;
		hasSelectHandler = onSelect != null;
		this.onSelect = onSelect == null ? function() {} : onSelect;
		this.enabled = enabled;
	}
}
