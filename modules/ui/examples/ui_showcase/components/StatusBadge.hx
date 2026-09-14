package components;

import Color;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Colored status line used for visible-range and live demo feedback. */
class StatusBadge {
	public static function build(key:String, label:String, color:Color):KeyedView
		return new KeyedView(key, new Text(label, null, color));
}
