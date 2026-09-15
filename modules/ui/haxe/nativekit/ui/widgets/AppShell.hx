package nativekit.ui.widgets;

import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.SizedBox;

/**
	Application chrome composition with optional top bar, side rails, and a
	growing main content slot. Routing, state, and slot-specific styling remain
	owned by the caller.
*/
class AppShell implements View {
	final key:Key;
	final topBar:Null<View>;
	final sidebar:Null<View>;
	final content:View;
	final inspector:Null<View>;
	public final style:LayoutStyle;
	public final bodyStyle:LayoutStyle;

	public function new(key:String, content:View, ?topBar:View, ?sidebar:View,
			?inspector:View, ?style:LayoutStyle, ?bodyStyle:LayoutStyle) {
		if (content == null)
			throw "AppShell requires a content view";
		this.key = new Key(key);
		this.content = content;
		this.topBar = topBar;
		this.sidebar = sidebar;
		this.inspector = inspector;
		this.style = style == null ? defaultStyle() : style.copy();
		this.bodyStyle = bodyStyle == null ? defaultBodyStyle() : bodyStyle.copy();
		this.bodyStyle.direction = LayoutDirection.LeftToRight;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var bodyChildren:Array<KeyedView> = [];
			if (sidebar != null)
				bodyChildren.push(new KeyedView("sidebar", sidebar));
			bodyChildren.push(new KeyedView("content", new SizedBox("content-slot",
				content, LayoutAxis.grow(), LayoutAxis.grow())));
			if (inspector != null)
				bodyChildren.push(new KeyedView("inspector", inspector));

			var children:Array<KeyedView> = [];
			if (topBar != null)
				children.push(new KeyedView("top-bar", topBar));
			children.push(new KeyedView("body", new Row("body", bodyChildren, bodyStyle)));
			return new Column("root", children, style).build(context);
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		return result;
	}

	static function defaultBodyStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		result.direction = LayoutDirection.LeftToRight;
		return result;
	}
}
