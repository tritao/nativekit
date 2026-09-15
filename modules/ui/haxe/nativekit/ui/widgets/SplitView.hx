package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutDirection;
import LayoutDistribution;
import LayoutStyle;
import LayoutVisualKind;
import LayoutWrapMode;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/**
	Two-pane composition with a controlled, draggable secondary extent. The
	caller owns persistence, routing, and responsive collapse policy.
*/
class SplitView implements View {
	final key:Key;
	final primary:View;
	final secondary:View;
	public final orientation:SplitOrientation;
	public final minimumExtent:Float;
	public final maximumExtent:Float;
	public final dividerExtent:Float;
	public final onResize:Null<Float->Void>;
	public final onCollapsedChanged:Null<Bool->Void>;
	public final style:LayoutStyle;
	public final dividerStyle:LayoutStyle;
	public var secondaryExtent:Float;
	public var collapsed:Bool;

	public function new(key:String, primary:View, secondary:View,
			?options:SplitViewOptions) {
		if (primary == null || secondary == null)
			throw "SplitView requires primary and secondary views";
		var resolved = options == null ? new SplitViewOptions() : options;
		if ((resolved.orientation != SplitOrientation.Horizontal &&
			resolved.orientation != SplitOrientation.Vertical) ||
			!finite(resolved.secondaryExtent) || !finite(resolved.minimumExtent) ||
			!finite(resolved.maximumExtent) || !finite(resolved.dividerExtent) ||
			resolved.minimumExtent < 0.0 || resolved.maximumExtent < resolved.minimumExtent ||
			resolved.dividerExtent < 0.0)
			throw "SplitView extent policy is invalid";
		this.key = new Key(key);
		this.primary = primary;
		this.secondary = secondary;
		this.orientation = resolved.orientation;
		this.minimumExtent = resolved.minimumExtent;
		this.maximumExtent = resolved.maximumExtent;
		this.dividerExtent = resolved.dividerExtent;
		this.secondaryExtent = clamp(resolved.secondaryExtent, minimumExtent, maximumExtent);
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
			var extent = collapsed ? 0.0 : boundedExtent();

			var primaryPane = new RenderNode(context.id("primary-pane"),
				LayoutVisualKind.Box, paneStyle(horizontal, true, 0.0));
			var primaryNode = context.withScope(new Key("primary"), function() {
				return primary.build(context);
			});
			primaryPane.add(primaryNode);
			root.add(primaryPane);

			if (dividerExtent > 0.0) {
				var divider = new RenderNode(context.id("divider"), LayoutVisualKind.Box,
					dividerLayoutStyle(horizontal));
				divider.semantics = new Semantics(AccessibilityRole.Separator);
				installDividerHandlers(divider, horizontal);
				root.add(divider);
			}

			var secondaryPane = new RenderNode(context.id("secondary-pane"),
				LayoutVisualKind.Box, paneStyle(horizontal, false, extent));
			secondaryPane.layout.style.visible = !collapsed;
			var secondaryNode = context.withScope(new Key("secondary"), function() {
				return secondary.build(context);
			});
			secondaryPane.add(secondaryNode);
			root.add(secondaryPane);
			return root;
		});
	}

	function installDividerHandlers(divider:RenderNode, horizontal:Bool):Void {
		var dragging = false;
		var startPointer = 0.0;
		var dragExtent = 0.0;
		divider.on(UiEventKind.PointerDown, function(event:UiEvent) {
			if (event.button != 0)
				return;
			dragging = true;
			startPointer = horizontal ? event.x : event.y;
			dragExtent = collapsed ? minimumExtent : boundedExtent();
			event.preventDefault();
		});
		divider.on(UiEventKind.PointerMove, function(event:UiEvent) {
			if (!dragging)
				return;
			var pointer = horizontal ? event.x : event.y;
			var next = clamp(dragExtent - (pointer - startPointer), minimumExtent,
				maximumExtent);
			secondaryExtent = next;
			if (collapsed && next > 0.0) {
				collapsed = false;
				if (onCollapsedChanged != null)
					onCollapsedChanged(false);
			}
			if (onResize != null)
				onResize(next);
			event.preventDefault();
		});
		divider.on(UiEventKind.PointerUp, function(_) { dragging = false; });
		divider.on(UiEventKind.PointerCancel, function(_) { dragging = false; });
	}

	function paneStyle(horizontal:Bool, primaryPane:Bool, extent:Float):LayoutStyle {
		var result = new LayoutStyle();
		if (horizontal) {
			result.width = primaryPane ? LayoutAxis.grow() : secondaryAxis(extent);
			result.height = LayoutAxis.grow();
		} else {
			result.width = LayoutAxis.grow();
			result.height = primaryPane ? LayoutAxis.grow() : secondaryAxis(extent);
		}
		return result;
	}

	static function secondaryAxis(extent:Float):LayoutAxis {
		// Clay's fixed-size representation treats zero as an open upper bound.
		// Percent zero is the unambiguous collapsed extent.
		return extent <= 0.0 ? LayoutAxis.percent(0.0) : LayoutAxis.fixed(extent);
	}

	function boundedExtent():Float {
		if (!finite(secondaryExtent))
			return minimumExtent;
		return clamp(secondaryExtent, minimumExtent, maximumExtent);
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
