package components;

import Color;
import LayoutStyle;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Overview card composed from ordinary NativeKit layout and text widgets. */
class DemoCard {
	public static function build(key:String, title:String, value:String, description:String,
			style:LayoutStyle, textColor:Color, mutedColor:Color, eyebrowColor:Color):Column {
		return new Column(key, [
			new KeyedView("eyebrow", new Text(title, null, eyebrowColor)),
			new KeyedView("value", new Text(value, null, textColor)),
			new KeyedView("description", new Text(description, null, mutedColor))
		], style);
	}
}
