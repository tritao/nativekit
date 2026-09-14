package components;

import Color;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Text;

/** Syntax-colored snippet line for the inspector's API documentation view. */
class CodeSample {
	public static function build(key:String, code:String, color:Color):KeyedView
		return new KeyedView(key, new Text(code, null, color));
}
