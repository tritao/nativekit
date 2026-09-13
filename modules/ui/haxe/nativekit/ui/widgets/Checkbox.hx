package nativekit.ui.widgets;

import LayoutStyle;
import nativekit.ui.core.View;

/** Accessible boolean checkbox composed from Haxe layout and paint nodes. */
class Checkbox extends BinaryControl implements View {
	public function new(key:String, label:String, checked:Bool = false,
			?onChange:Bool->Void, ?style:LayoutStyle) {
		super(key, label, checked, false, onChange, style);
	}
}
