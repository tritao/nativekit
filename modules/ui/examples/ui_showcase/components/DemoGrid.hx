package components;

import LayoutStyle;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Simple responsive row container for peer demo cards. */
class DemoGrid {
	public static function build(key:String, children:Array<KeyedView>, style:LayoutStyle):Row
		return new Row(key, children, style);
}
