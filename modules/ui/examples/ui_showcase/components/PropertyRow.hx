package components;

import nativekit.ui.theme.TextRole;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Compact inspector line for a resolved property or runtime state value. */
class PropertyRow {
	public static function build(key:String, value:String):KeyedView
		return new KeyedView(key, new Text(value, null, null, null, TextRole.Caption));
}
