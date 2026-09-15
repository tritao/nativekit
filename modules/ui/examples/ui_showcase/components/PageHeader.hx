package components;

import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;
import nativekit.ui.theme.TextRole;

/** Standard Explorer page title and supporting description. */
class PageHeader {
	public static function append(items:Array<KeyedView>, title:String, description:String):Void {
		items.push(new KeyedView("page-title", new Text(title, null, null, null, TextRole.Heading)));
		items.push(new KeyedView("page-description",
			new Text(description, null, null, null, TextRole.Caption)));
	}
}
