package components;

import Color;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Standard Explorer page title and supporting description. */
class PageHeader {
	public static function append(items:Array<KeyedView>, title:String, description:String,
			textColor:Color, mutedColor:Color):Void {
		items.push(new KeyedView("page-title", new Text(title, null, textColor)));
		items.push(new KeyedView("page-description", new Text(description, null, mutedColor)));
	}
}
