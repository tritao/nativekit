package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutDirection;
import LayoutDistribution;
import LayoutStyle;
import LayoutVisualKind;
import LayoutWrapMode;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CursorShape;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/**
	Two-pane composition with a controlled, draggable leading or trailing
	pane. The caller owns persistence, routing, and responsive collapse policy.
*/
class SplitView implements View {
	final key:Key;
	final leading:View;
	final trailing:View;
	public final orientation:SplitOrientation;
	public final resizableSide:SplitSide;
	public final minimumExtent:Float;
	public final maximumExtent:Float;
	public final dividerExtent:Float;
	public final onResize:Null<Float->Void>;
	public final onCollapsedChanged:Null<Bool->Void>;
	public final style:LayoutStyle;
	public final dividerStyle:LayoutStyle;
	public var extent:Float;
	public var collapsed:Bool;

	public function new(key:String, leading:View, trailing:View,
			?options:SplitViewOptions) {
		if (leading == null || trailing == null)
			throw "SplitView requires leading and trailing views";
		var resolved = options == null ? new SplitViewOptions() : options;
		if ((resolved.orientation != SplitOrientation.Horizontal &&
			resolved.orientation != SplitOrientation.Vertical) ||
			(resolved.resizableSide != SplitSide.Leading &&
				resolved.resizableSide != SplitSide.Trailing) ||
			!finite(resolved.extent) || !finite(resolved.minimumExtent) ||
			!finite(resolved.maximumExtent) || !finite(resolved.dividerExtent) ||
			resolved.minimumExtent < 0.0 || resolved.maximumExtent < resolved.minimumExtent ||
			resolved.dividerExtent < 0.0)
			throw "SplitView extent policy is invalid";
		this.key = new Key(key);
		this.leading = leading;
		this.trailing = trailing;
		this.orientation = resolved.orientation;
		this.resizableSide = resolved.resizableSide;
		this.minimumExtent = resolved.minimumExtent;
		this.maximumExtent = resolved.maximumExtent;
		this.dividerExtent = resolved.dividerExtent;
		this.extent = clamp(resolved.extent, minimumExtent, maximumExtent);
		this.collapsed = resolved.collapsed;
		this.onResize = resolved.onResize;
		this.onCollapsedChanged = resolved.onCollapsedChanged;
		this.style = resolved.style == null ? defaultStyle() : resolved.style.copy();
		this.dividerStyle = resolved.dividerStyle == null
			? defaultDividerStyle() : resolved.dividerStyle.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var horizontal = orientation == SplitOrientation.Horizontal;
			var rootStyle = style.copy();
			rootStyle.direction = horizontal ? LayoutDirection.LeftToRight : LayoutDirection.TopToBottom;
			rootStyle.childGap = 0.0;
			rootStyle.childDistribution = LayoutDistribution.Start;
			rootStyle.wrapMode = LayoutWrapMode.NoWrap;
			var root = new RenderNode(context.id("split-view"), LayoutVisualKind.Box, rootStyle);
			var paneExtent = collapsed ? 0.0 : boundedExtent();

			var leadingPane = new RenderNode(context.id("leading-pane"), LayoutVisualKind.Box,
				paneStyle(horizontal, resizableSide == SplitSide.Leading, paneExtent));
			leadingPane.layout.style.visible = !collapsed || resizableSide != SplitSide.Leading;
			var leadingNode = context.withScope(new Key("leading"), function() {
				return leading.build(context);
			});
			leadingPane.add(leadingNode);
			root.add(leadingPane);

			if (dividerExtent > 0.0) {
				var divider = new RenderNode(context.id("divider"), LayoutVisualKind.Box,
					dividerLayoutStyle(horizontal));
				divider.focusable = true;
				divider.cursor = horizontal ? CursorShape.HorizontalResize : CursorShape.VerticalResize;
				var sideName = resizableSide == SplitSide.Leading ? "Leading" : "Trailing";
				var dividerSemantics = new Semantics(AccessibilityRole.Separator,
					'$sideName pane divider');
				dividerSemantics.actions = AccessibilityAction.Increment |
					AccessibilityAction.Decrement;
				dividerSemantics.numericValue = boundedExtent();
				dividerSemantics.numericMinimum = minimumExtent;
				dividerSemantics.numericMaximum = maximumExtent;
				divider.semantics = dividerSemantics;
				var dragState:State<SplitDragState> = context.state(divider.id,
					new SplitDragState());
				installDividerHandlers(divider, horizontal, dragState);
				root.add(divider);
			}

			var trailingPane = new RenderNode(context.id("trailing-pane"), LayoutVisualKind.Box,
				paneStyle(horizontal, resizableSide == SplitSide.Trailing, paneExtent));
			trailingPane.layout.style.visible = !collapsed || resizableSide != SplitSide.Trailing;
			var trailingNode = context.withScope(new Key("trailing"), function() {
				return trailing.build(context);
			});
			trailingPane.add(trailingNode);
			root.add(trailingPane);
			return root;
		});
	}

	function installDividerHandlers(divider:RenderNode, horizontal:Bool,
			dragState:State<SplitDragState>):Void {
		divider.on(UiEventKind.PointerDown, function(event:UiEvent) {
			if (event.button != 0)
				return;
			var drag = dragState.value;
			drag.dragging = true;
			drag.startPointer = horizontal ? event.x : event.y;
			drag.startExtent = collapsed ? minimumExtent : boundedExtent();
			dragState.update(drag);
			event.preventDefault();
		});
		divider.on(UiEventKind.PointerMove, function(event:UiEvent) {
			var drag = dragState.value;
			if (!drag.dragging)
				return;
			var pointer = horizontal ? event.x : event.y;
			var delta = pointer - drag.startPointer;
			var direction = resizableSide == SplitSide.Leading ? 1.0 : -1.0;
			updateExtent(drag.startExtent + delta * direction, true);
			event.preventDefault();
		});
		var handleKey = function(event:UiEvent) {
			var delta = 0.0;
			var positive = horizontal
				? (resizableSide == SplitSide.Leading ? UiKey.Right : UiKey.Left)
				: (resizableSide == SplitSide.Leading ? UiKey.Down : UiKey.Up);
			var negative = horizontal
				? (resizableSide == SplitSide.Leading ? UiKey.Left : UiKey.Right)
				: (resizableSide == SplitSide.Leading ? UiKey.Up : UiKey.Down);
			if (event.key == positive)
				delta = keyboardStep();
			else if (event.key == negative)
				delta = -keyboardStep();
			else if (event.key == UiKey.Home)
				updateExtent(minimumExtent, true);
			else if (event.key == UiKey.End)
				updateExtent(maximumExtent, true);
			else
				return;
			if (delta != 0.0)
				updateExtent(extent + delta, true);
			event.preventDefault();
		};
		divider.on(UiEventKind.KeyDown, handleKey);
		divider.on(UiEventKind.KeyRepeat, handleKey);
		divider.on(UiEventKind.AccessibilityIncrement, function(event) {
			updateExtent(extent + keyboardStep() * actionGranularity(event), true);
		});
		divider.on(UiEventKind.AccessibilityDecrement, function(event) {
			updateExtent(extent - keyboardStep() * actionGranularity(event), true);
		});
		divider.on(UiEventKind.PointerUp, function(_) {
			var drag = dragState.value;
			drag.dragging = false;
			dragState.update(drag);
		});
		divider.on(UiEventKind.PointerCancel, function(_) {
			var drag = dragState.value;
			drag.dragging = false;
			dragState.update(drag);
		});
	}

	function updateExtent(next:Float, expandCollapsed:Bool):Bool {
		if (!finite(next))
			return false;
		var bounded = clamp(next, minimumExtent, maximumExtent);
		var changed = bounded != extent;
		extent = bounded;
		if (collapsed && expandCollapsed && bounded > 0.0) {
			collapsed = false;
			if (onCollapsedChanged != null)
				onCollapsedChanged(false);
		}
		if (changed && onResize != null)
			onResize(bounded);
		return changed;
	}

	static inline function keyboardStep():Float
		return 8.0;

	static function actionGranularity(event:UiEvent):Int {
		var action:AccessibilityActionData = cast event.data;
		return action != null && action.granularity > 1 ? action.granularity : 1;
	}

	function paneStyle(horizontal:Bool, resizable:Bool, paneExtent:Float):LayoutStyle {
		var result = new LayoutStyle();
		if (horizontal) {
			result.width = resizable ? extentAxis(paneExtent) : LayoutAxis.grow();
			result.height = LayoutAxis.grow();
		} else {
			result.width = LayoutAxis.grow();
			result.height = resizable ? extentAxis(paneExtent) : LayoutAxis.grow();
		}
		return result;
	}

	static function extentAxis(paneExtent:Float):LayoutAxis {
		// Clay's fixed-size representation treats zero as an open upper bound.
		// Percent zero is the unambiguous collapsed extent.
		return paneExtent <= 0.0 ? LayoutAxis.percent(0.0) : LayoutAxis.fixed(paneExtent);
	}

	function boundedExtent():Float {
		if (!finite(extent))
			return minimumExtent;
		return clamp(extent, minimumExtent, maximumExtent);
	}

	function dividerLayoutStyle(horizontal:Bool):LayoutStyle {
		var result = dividerStyle.copy();
		if (horizontal) {
			result.width = LayoutAxis.fixed(dividerExtent);
			result.height = LayoutAxis.grow();
		} else {
			result.width = LayoutAxis.grow();
			result.height = LayoutAxis.fixed(dividerExtent);
		}
		return result;
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.grow();
		return result;
	}

	static function defaultDividerStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.background = Color.rgba(0.0, 0.0, 0.0, 0.12);
		return result;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}

/** Pointer-drag state that survives the frame-local view rebuild. */
private class SplitDragState {
	public var dragging:Bool;
	public var startPointer:Float;
	public var startExtent:Float;

	public function new() {
		dragging = false;
		startPointer = 0.0;
		startExtent = 0.0;
	}
}
