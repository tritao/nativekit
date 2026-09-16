package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Clipped Haxe scroll container translated from its persistent controller offset. */
class ScrollView implements View {
	final key:Key;
	final child:View;
	final axis:Int;
	public final style:LayoutStyle;
	public var controller(default, null):ScrollController;
	public var onScroll:UiEvent->Void;

	public function new(key:String, child:View, ?style:LayoutStyle,
			axis:Int = ScrollAxis.Vertical, ?controller:ScrollController) {
		if (child == null || (axis != ScrollAxis.Vertical && axis != ScrollAxis.Horizontal &&
			axis != ScrollAxis.Both))
			throw "ScrollView requires a child and a supported axis";
		this.key = new Key(key);
		this.child = child;
		this.axis = axis;
		this.style = style == null ? new LayoutStyle() : style.copy();
		this.controller = controller == null ? new ScrollController() : controller;
		onScroll = null;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var viewport = new RenderNode(context.id("scroll"), LayoutVisualKind.Box, style.copy());
			var semantics = new Semantics(AccessibilityRole.ScrollArea);
			semantics.actions = AccessibilityAction.ScrollForward | AccessibilityAction.ScrollBackward;
			viewport.semantics = semantics;
			viewport.layout.style.clipHorizontal = viewport.layout.style.clipHorizontal ||
				axis == ScrollAxis.Horizontal || axis == ScrollAxis.Both;
			viewport.layout.style.clipVertical = viewport.layout.style.clipVertical ||
				axis == ScrollAxis.Vertical || axis == ScrollAxis.Both;
			var stored:State<ScrollController> = context.state(viewport.id, controller);
			controller = stored.value;
			controller.bind(function(value) {
				stored.update(value);
			});

			var contentStyle = new LayoutStyle();
			contentStyle.width = axis == ScrollAxis.Vertical ? LayoutAxis.grow() : LayoutAxis.fit();
			contentStyle.height = axis == ScrollAxis.Horizontal ? LayoutAxis.grow() : LayoutAxis.fit();
			contentStyle.transform = Transform2D.identity().translated(-controller.offsetX,
				-controller.offsetY);
			var translatedContent = new RenderNode(context.id("scroll-content"),
				LayoutVisualKind.Box, contentStyle);
			var content = context.withScope(new Key("content"), function() return child.build(context));
			translatedContent.add(content);
			viewport.add(translatedContent);

			viewport.onResolved(function(geometry) {
				controller.updateMetrics(geometry.width, geometry.height,
					geometry.contentBounds.width, geometry.contentBounds.height);
			});
			viewport.on(UiEventKind.Scroll, function(event) {
				if (onScroll != null)
					onScroll(event);
				if (event.defaultPrevented)
					return;
				var dx = axis == ScrollAxis.Horizontal || axis == ScrollAxis.Both ? -event.deltaX : 0.0;
				var dy = axis == ScrollAxis.Vertical || axis == ScrollAxis.Both ? -event.deltaY : 0.0;
				if (controller.scrollBy(dx, dy))
					event.stopPropagation();
			});
			return viewport;
		});
	}
}
