package components;

import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;
import nativekit.ui.theme.TextRole;

/** Small muted label for catalog groups and demo sections. */
class SectionHeader {
	public static function build(key:String, label:String):KeyedView
		return new KeyedView(key, new Text(label, null, null, null, TextRole.Caption));
}
