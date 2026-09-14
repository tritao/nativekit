package components;

import Color;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Compact inspector line for a resolved property or runtime state value. */
class PropertyRow {
	public static function build(key:String, value:String, color:Color):KeyedView
		return new KeyedView(key, new Text(value, null, color));
}
