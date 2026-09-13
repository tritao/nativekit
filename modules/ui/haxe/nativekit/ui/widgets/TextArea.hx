package nativekit.ui.widgets;

import Color;
import LayoutStyle;
import TextStyle;
import nativekit.ui.core.View;

/** Multiline text editor sharing the TextField IME and selection model. */
class TextArea extends TextField implements View {
	public function new(key:String, value:String = "", ?onChange:String->Void,
			?style:LayoutStyle, ?label:String, ?textStyle:TextStyle, ?textColor:Color) {
		super(key, value, onChange, style, label, textStyle, textColor, true);
	}
}
