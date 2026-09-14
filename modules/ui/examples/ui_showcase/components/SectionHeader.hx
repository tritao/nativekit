package components;

import Color;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Small muted label for catalog groups and demo sections. */
class SectionHeader {
	public static function build(key:String, label:String, color:Color):KeyedView
		return new KeyedView(key, new Text(label, null, color));
}
