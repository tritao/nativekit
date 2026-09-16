package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.icons.IconName;
import nativekit.ui.widgets.Icon;
import nativekit.ui.widgets.IconButton;

/** Reusable single-line search input with leading affordance and clear action. */
class SearchField implements View {
	public final key:String;
	public var value:String;
	public var placeholder:String;
	public var label:String;
	public var enabled:Bool;
	public final style:LayoutStyle;
	public var onChange:Null<String->Void>;

	public function new(key:String, value:String = "", ?onChange:String->Void,
			?style:LayoutStyle, placeholder:String = "Search") {
		if (key == null || key.length == 0)
			throw "Search fields require a stable key";
		this.key = key;
		this.value = value == null ? "" : value;
		this.onChange = onChange;
		this.placeholder = placeholder == null || placeholder.length == 0 ? "Search" : placeholder;
		label = this.placeholder;
		enabled = true;
		this.style = style == null ? defaultStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var icon = new Icon("search-icon", IconName.Search, 16.0,
				context.theme.mutedText);

			var inputStyle = new LayoutStyle();
			inputStyle.width = LayoutAxis.grow();
			inputStyle.height = LayoutAxis.grow();
			inputStyle.padding = new Insets(0.0, 0.0, 0.0, 0.0);
			inputStyle.background = Color.rgba(0.0, 0.0, 0.0, 0.0);
			var input = new TextField("input", value, function(next) {
				value = next;
				if (onChange != null)
					onChange(next);
			}, inputStyle, label);
			input.placeholder = placeholder;
			input.enabled = enabled;

			var children:Array<KeyedView> = [
				new KeyedView("icon", icon),
				new KeyedView("input", input)
			];
			if (value.length > 0) {
				var clearStyle = new LayoutStyle();
				clearStyle.width = LayoutAxis.fixed(24.0);
				clearStyle.height = LayoutAxis.grow();
				clearStyle.padding = new Insets(2.0, 2.0, 2.0, 2.0);
				clearStyle.background = Color.rgba(0.0, 0.0, 0.0, 0.0);
				var clear = new IconButton("clear", IconName.Close, "Clear search", function() {
					value = "";
					if (onChange != null)
						onChange("");
				}, clearStyle);
				clear.enabled = enabled;
				children.push(new KeyedView("clear", clear));
			}
			return new Row("search-field", children, style).build(context);
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(38.0);
		result.padding = new Insets(9.0, 7.0, 9.0, 7.0);
		result.childGap = 6.0;
		result.childAlignY = LayoutAlignmentY.Center;
		return result;
	}
}
