package nativekit.ui.widgets;

import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import ResolvedLayoutItem;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.style.StyleTarget;

/** Clipped Haxe scroll container translated from its persistent controller offset. */
class ScrollView implements View {
	final key:Key;
	final child:View;
	final axis:Int;
	public final style:LayoutStyle;
	public var controller(default, null):ScrollController;
	public var onScroll:UiEvent->Void;
	/** Shows an interactive overlay scrollbar when vertical content overflows. */
	public var showScrollbar:Bool;

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
		showScrollbar = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var viewportStyle = style.copy();
			var id = context.id("scroll");
			var flags = context.interactionStates.get(id);
			var computed = context.resolveStyle(new StyleTarget("scroll-view", key.value, key.value,
				null, ["scroll-view"], flags), viewportStyle);
			var viewport = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			viewport.setStyleIdentity("scroll-view", key.value, key.value, null, ["scroll-view"]);
			viewport.states = flags;
			viewport.computedStyle = computed;
			viewport.focusable = true;
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
			var content = context.withStyleParent(computed, function() {
				return context.withScope(new Key("content"), function() return child.build(context));
			});
			translatedContent.add(content);
			viewport.add(translatedContent);
			if (showScrollbar && axis != ScrollAxis.Horizontal && controller.maxScrollY > 0.0 &&
				controller.viewportHeight > 0.0)
				addVerticalScrollbar(context, viewport, stored);

			viewport.onResolved(function(geometry) {
				controller.updateMetrics(geometry.width, geometry.height,
					geometry.contentBounds.width, geometry.contentBounds.height);
			});
			viewport.on(UiEventKind.Scroll, function(event) {
				if (onScroll != null)
					onScroll(event);
				if (event.defaultPrevented)
					return;
				var dx = axis == ScrollAxis.Horizontal || axis == ScrollAxis.Both ? event.deltaX : 0.0;
				var dy = axis == ScrollAxis.Vertical || axis == ScrollAxis.Both ? event.deltaY : 0.0;
				if (controller.scrollBy(dx, dy))
					event.stopPropagation();
			});
			var handleKey = function(event:UiEvent) {
				var amount = 0.0;
				if (event.key == UiKey.Down)
					amount = 40.0;
				else if (event.key == UiKey.Up)
					amount = -40.0;
				else if (event.key == UiKey.PageDown)
					amount = controller.viewportHeight * 0.9;
				else if (event.key == UiKey.PageUp)
					amount = -controller.viewportHeight * 0.9;
				else if (event.key == UiKey.Home) {
					if (controller.jumpTo(controller.offsetX, 0.0))
						event.preventDefault();
					return;
				} else if (event.key == UiKey.End) {
					if (controller.jumpTo(controller.offsetX, controller.maxScrollY))
						event.preventDefault();
					return;
				} else
					return;
				if (controller.scrollBy(0.0, amount))
					event.preventDefault();
			};
			viewport.on(UiEventKind.KeyDown, handleKey);
			viewport.on(UiEventKind.KeyRepeat, handleKey);
			return viewport;
		});
	}

	function addVerticalScrollbar(context:BuildContext, viewport:RenderNode,
			stored:State<ScrollController>):Void {
		var trackWidth = 10.0;
		var inset = 2.0;
		var trackHeight = Math.max(0.0, controller.viewportHeight - inset * 2.0);
		var thumbHeight = Math.max(24.0,
			trackHeight * controller.viewportHeight / controller.contentHeight);
		if (thumbHeight > trackHeight)
			thumbHeight = trackHeight;
		var travel = Math.max(0.0, trackHeight - thumbHeight);
		var thumbY = inset + (controller.maxScrollY <= 0.0 ? 0.0 :
			controller.offsetY / controller.maxScrollY * travel);
		var trackStyle = new LayoutStyle();
		trackStyle.positioning = LayoutPositioning.Absolute;
		trackStyle.positionX = Math.max(0.0, controller.viewportWidth - trackWidth - inset);
		trackStyle.positionY = inset;
		trackStyle.width = LayoutAxis.fixed(trackWidth);
		trackStyle.height = LayoutAxis.fixed(trackHeight);
		trackStyle.background = context.theme.controlUnselected;
		trackStyle.radiusTopLeft = trackStyle.radiusTopRight = trackWidth * 0.5;
		trackStyle.radiusBottomLeft = trackStyle.radiusBottomRight = trackWidth * 0.5;
		trackStyle.zIndex = 100;
		var track = new RenderNode(context.id("vertical-scrollbar-track"),
			LayoutVisualKind.Box, trackStyle);
		track.setStyleIdentity("scrollbar-track", key.value, key.value, null,
			["scrollbar", "vertical"]);
		var thumbStyle = new LayoutStyle();
		thumbStyle.positioning = LayoutPositioning.Absolute;
		thumbStyle.positionX = 1.0;
		thumbStyle.positionY = thumbY - inset;
		thumbStyle.width = LayoutAxis.fixed(trackWidth - 2.0);
		thumbStyle.height = LayoutAxis.fixed(thumbHeight);
		thumbStyle.background = context.theme.accent;
		thumbStyle.radiusTopLeft = thumbStyle.radiusTopRight = (trackWidth - 2.0) * 0.5;
		thumbStyle.radiusBottomLeft = thumbStyle.radiusBottomRight = (trackWidth - 2.0) * 0.5;
		thumbStyle.zIndex = 101;
		var thumbId = context.id("vertical-scrollbar-thumb");
		var thumb = new RenderNode(thumbId, LayoutVisualKind.Box, thumbStyle);
		thumb.setStyleIdentity("scrollbar-thumb", key.value, key.value, null,
			["scrollbar", "vertical"]);
		thumb.focusable = true;
		var semantics = new Semantics(AccessibilityRole.Slider, "Vertical scroll position");
		semantics.actions = AccessibilityAction.Increment | AccessibilityAction.Decrement;
		semantics.numericMinimum = 0.0;
		semantics.numericMaximum = controller.maxScrollY;
		semantics.numericValue = controller.offsetY;
		semantics.orientation = AccessibilityOrientation.Vertical;
		thumb.semantics = semantics;
		var dragState:State<ScrollbarDragState> = context.state(thumbId,
			new ScrollbarDragState());
		thumb.on(UiEventKind.PointerDown, function(event) {
			if (event.button != 0)
				return;
			dragState.value.dragging = true;
			dragState.value.pointerY = event.y;
			dragState.value.offsetY = controller.offsetY;
			dragState.update(dragState.value);
			event.capturePointer();
			event.stopPropagation();
			event.preventDefault();
		});
		thumb.on(UiEventKind.PointerMove, function(event) {
			if (!dragState.value.dragging || travel <= 0.0)
				return;
			controller.jumpTo(controller.offsetX, dragState.value.offsetY +
				(event.y - dragState.value.pointerY) * controller.maxScrollY / travel);
			event.preventDefault();
		});
		var finishDrag = function(event:UiEvent) {
			if (!dragState.value.dragging)
				return;
			dragState.value.dragging = false;
			dragState.update(dragState.value);
			event.releasePointer();
			event.preventDefault();
		};
		thumb.on(UiEventKind.PointerUp, finishDrag);
		thumb.on(UiEventKind.PointerCancel, finishDrag);
		thumb.on(UiEventKind.AccessibilityIncrement, function(event) {
			if (controller.scrollBy(0.0, Math.max(40.0, controller.viewportHeight * 0.1)))
				event.preventDefault();
		});
		thumb.on(UiEventKind.AccessibilityDecrement, function(event) {
			if (controller.scrollBy(0.0, -Math.max(40.0, controller.viewportHeight * 0.1)))
				event.preventDefault();
		});
		track.on(UiEventKind.PointerDown, function(event) {
			if (event.button != 0 || track.resolved == null)
				return;
			var geometry:ResolvedLayoutItem = cast track.resolved;
			var local = geometry.viewportToLayout(event.x, event.y);
			var page = controller.viewportHeight * 0.9;
			controller.scrollBy(0.0, local.y < geometry.y + thumbY - inset ? -page : page);
			event.preventDefault();
		});
		track.add(thumb);
		viewport.add(track);
	}
}

private class ScrollbarDragState {
	public var dragging:Bool = false;
	public var pointerY:Float = 0.0;
	public var offsetY:Float = 0.0;
	public function new() {}
}
