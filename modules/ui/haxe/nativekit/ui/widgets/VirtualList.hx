package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.widgets.Spacer;

/** Fixed-height virtual list composed from ordinary rows inside ScrollView. */
class VirtualList implements View {
	public final key:String;
	public final itemCount:Int;
	public final itemHeight:Float;
	public final viewportStyle:LayoutStyle;
	public var controller(default, null):ScrollController;
	final itemBuilder:Int->View;
	final keyForIndex:Null<Int->String>;
	final fallbackViewportHeight:Float;

	public function new(key:String, itemCount:Int, itemHeight:Float, itemBuilder:Int->View,
			?viewportStyle:LayoutStyle, ?keyForIndex:Int->String, ?controller:ScrollController,
			viewportHeight:Float = 300.0) {
		if (key == null || key.length == 0 || itemCount < 0 || itemHeight <= 0.0 ||
			!finite(itemHeight) || itemBuilder == null || viewportHeight <= 0.0 ||
			!finite(viewportHeight))
			throw "VirtualList requires a stable key, valid dimensions, and an item builder";
		this.key = key;
		this.itemCount = itemCount;
		this.itemHeight = itemHeight;
		this.itemBuilder = itemBuilder;
		this.keyForIndex = keyForIndex;
		this.controller = controller == null ? new ScrollController() : controller;
		this.fallbackViewportHeight = viewportHeight;
		this.viewportStyle = viewportStyle == null ? defaultViewportStyle(viewportHeight) :
			viewportStyle.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var stateId = context.id("scroll-state");
			var stored:State<ScrollController> = context.state(stateId, controller);
			controller = cast stored.value;
			var viewportHeight = controller.viewportHeight > 0.0 ? controller.viewportHeight :
				(viewportStyle.height.sizing == LayoutSizing.Fixed ? viewportStyle.height.value :
				fallbackViewportHeight);
			var first = itemCount == 0 ? 0 : Std.int(controller.offsetY / itemHeight);
			if (first > 0)
				first--;
			if (first > itemCount)
				first = itemCount;
			var last = Std.int((controller.offsetY + viewportHeight) / itemHeight) + 2;
			if (last > itemCount)
				last = itemCount;
			if (last < first)
				last = first;

			var rowViews:Array<KeyedView> = [];
			var beforeHeight = first * itemHeight;
			rowViews.push(new KeyedView("before", new Spacer("before-spacer",
				LayoutAxis.grow(), LayoutAxis.fixed(beforeHeight))));
			for (index in first...last) {
				var rowKey = keyForIndex == null ? Std.string(index) : keyForIndex(index);
				if (rowKey == null || rowKey.length == 0)
					throw 'VirtualList item $index has an empty key';
				var item = itemBuilder(index);
				if (item == null)
					throw 'VirtualList item builder returned null for index $index';
				var row = new SizedBox("row", item, LayoutAxis.grow(),
					LayoutAxis.fixed(itemHeight));
				rowViews.push(new KeyedView('item:$rowKey', row));
			}
			var afterHeight = (itemCount - last) * itemHeight;
			rowViews.push(new KeyedView("after", new Spacer("after-spacer",
				LayoutAxis.grow(), LayoutAxis.fixed(afterHeight))));

			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.grow();
			contentStyle.height = LayoutAxis.fixed(itemCount * itemHeight);
			var content = new Column("virtual-content", rowViews, contentStyle);
			var scroll = new ScrollView("viewport", content, viewportStyle,
				ScrollAxis.Vertical, controller);
			var root = new RenderNode(context.id("list"), LayoutVisualKind.Box);
			root.layout.style.width = viewportStyle.width;
			root.layout.style.height = viewportStyle.height;
			root.semantics = new Semantics(AccessibilityRole.List);
			var viewport = context.withScope(new Key("scroll-view"), function() return scroll.build(context));
			root.add(viewport);
			return root;
		});
	}

	static function defaultViewportStyle(height:Float):LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(height);
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
