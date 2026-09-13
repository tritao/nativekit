package nativekit.ui.widgets;

import LayoutStyle;
import nativekit.ui.core.View;

/** Accessible switch-style boolean control composed on the Haxe side. */
class Toggle extends BinaryControl implements View {
	public function new(key:String, label:String, checked:Bool = false,
			?onChange:Bool->Void, ?style:LayoutStyle) {
		super(key, label, checked, true, onChange, style);
	}
}
