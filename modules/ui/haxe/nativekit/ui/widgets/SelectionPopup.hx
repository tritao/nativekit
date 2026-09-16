package nativekit.ui.widgets;

import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Mutable bridge for active-option visibility across rebuilt render trees. */
class SelectionPopupVisibility {
	public var ensure:Int->Void;

	public function new() {
		ensure = function(_:Int) {};
	}
}

/** Result of building a shared selection popup. */
class SelectionPopupResult {
	public final node:RenderNode;
	public final optionNodes:Array<RenderNode>;
	public final ensureActiveVisible:Int->Void;

	public function new(node:RenderNode, optionNodes:Array<RenderNode>,
			ensureActiveVisible:Int->Void) {
		this.node = node;
		this.optionNodes = optionNodes;
		this.ensureActiveVisible = ensureActiveVisible;
	}
}

/** Internal popup/list kernel shared by Select and ComboBox. */
class SelectionPopup {
	final options:Array<SelectOption<Dynamic>>;
	final visibleIndices:Array<Int>;
	final selectedValue:Dynamic;
	final activeIndex:Int;
	final rootX:Float;
	final rootY:Float;
	final triggerX:Float;
	final triggerY:Float;
	final triggerWidth:Float;
	final triggerHeight:Float;
	final fallbackWidth:Float;
	final fallbackHeight:Float;
	final onSelect:Int->Void;
	final onClose:Void->Void;
	final onActive:Int->Void;
	final isEnabled:Int->Bool;
	final nextEnabled:Int->Int->Int;

	public function new(options:Array<SelectOption<Dynamic>>, visibleIndices:Array<Int>,
			selectedValue:Dynamic, activeIndex:Int, rootX:Float, rootY:Float,
			triggerX:Float, triggerY:Float, triggerWidth:Float, triggerHeight:Float,
			fallbackWidth:Float, fallbackHeight:Float, onSelect:Int->Void,
			onClose:Void->Void, onActive:Int->Void, isEnabled:Int->Bool,
			nextEnabled:Int->Int->Int) {
		this.options = options;
		this.visibleIndices = visibleIndices;
		this.selectedValue = selectedValue;
		this.activeIndex = activeIndex;
		this.rootX = rootX;
		this.rootY = rootY;
		this.triggerX = triggerX;
		this.triggerY = triggerY;
		this.triggerWidth = triggerWidth;
		this.triggerHeight = triggerHeight;
		this.fallbackWidth = fallbackWidth;
		this.fallbackHeight = fallbackHeight;
		this.onSelect = onSelect == null ? function(_:Int) {} : onSelect;
		this.onClose = onClose == null ? function() {} : onClose;
		this.onActive = onActive == null ? function(_:Int) {} : onActive;
		this.isEnabled = isEnabled == null ? function(_:Int) return true : isEnabled;
		this.nextEnabled = nextEnabled == null ? function(index:Int, _:Int) return index : nextEnabled;
	}

	public function build(context:BuildContext, root:RenderNode):SelectionPopupResult {
		var optionNodes:Array<RenderNode> = [];
		var scrollController:ScrollController = null;
		var visibility = new SelectionPopupVisibility();
		var width = triggerWidth > 0.0 ? triggerWidth : fallbackWidth;
		var height = triggerHeight > 0.0 ? triggerHeight : fallbackHeight;
		var edge = Math.min(8.0, context.viewportWidth * 0.5);
		var availableWidth = Math.max(1.0, context.viewportWidth - edge * 2.0);
		var popupWidth = Math.min(Math.max(1.0, width), availableWidth);
		var popupX = clamp(triggerX, edge, Math.max(edge,
			context.viewportWidth - edge - popupWidth));
		var rowHeight = 32.0;
		var popupPadding = 8.0;
		var popupGap = visibleIndices.length > 0 ? (visibleIndices.length - 1) * 2.0 : 0.0;
		var naturalHeight = popupPadding + visibleIndices.length * rowHeight + popupGap;
		var placementGap = 4.0;
		var below = Math.max(0.0, context.viewportHeight - (triggerY + height) - placementGap);
		var above = Math.max(0.0, triggerY - placementGap);
		var opensBelow = below >= naturalHeight || below >= above;
		var availableHeight = opensBelow ? below : above;
		var popupHeight = Math.min(Math.max(1.0, naturalHeight), Math.max(1.0, availableHeight));
		var popupGlobalY = opensBelow ? triggerY + height + placementGap :
			triggerY - popupHeight - placementGap;
		var needsScroll = popupHeight + 0.001 < naturalHeight;
		var optionViewportHeight = Math.max(1.0, popupHeight - popupPadding);

		var dropdownStyle = new LayoutStyle();
		dropdownStyle.width = LayoutAxis.fixed(popupWidth);
		dropdownStyle.height = LayoutAxis.fixed(popupHeight);
		dropdownStyle.direction = LayoutDirection.TopToBottom;
		dropdownStyle.positioning = LayoutPositioning.Absolute;
		dropdownStyle.positionX = popupX - rootX;
		dropdownStyle.positionY = popupGlobalY - rootY;
		dropdownStyle.zIndex = 10;
		dropdownStyle.clipToParent = false;
		dropdownStyle.padding = new Insets(4.0, 4.0, 4.0, 4.0);
		dropdownStyle.childGap = 2.0;
		dropdownStyle.background = context.theme.panelBackground;
		dropdownStyle.radiusTopLeft = dropdownStyle.radiusTopRight = 5.0;
		dropdownStyle.radiusBottomLeft = dropdownStyle.radiusBottomRight = 5.0;
		var dropdown = new RenderNode(context.id("options"), LayoutVisualKind.Box, dropdownStyle);
		var listSemantics = new Semantics(AccessibilityRole.List, "Options");
		listSemantics.orientation = AccessibilityOrientation.Vertical;
		if (needsScroll)
			listSemantics.actions = AccessibilityAction.ScrollForward |
				AccessibilityAction.ScrollBackward;
		dropdown.semantics = listSemantics;
		var optionParent = dropdown;
		if (needsScroll) {
			var viewportStyle = new LayoutStyle();
			viewportStyle.width = LayoutAxis.grow();
			viewportStyle.height = LayoutAxis.fixed(optionViewportHeight);
			viewportStyle.clipVertical = true;
			var scrollViewport = new RenderNode(context.id("option-viewport"),
				LayoutVisualKind.Box, viewportStyle);
			var storedScroll:State<ScrollController> = context.state(scrollViewport.id,
				new ScrollController());
			scrollController = storedScroll.value;
			scrollController.bind(function(value) { storedScroll.update(value); });
			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.grow();
			contentStyle.height = LayoutAxis.fit();
			contentStyle.direction = LayoutDirection.TopToBottom;
			contentStyle.transform = Transform2D.identity().translated(0.0,
				-scrollController.offsetY);
			var optionContent = new RenderNode(context.id("option-content"),
				LayoutVisualKind.Box, contentStyle);
			scrollViewport.add(optionContent);
			dropdown.add(scrollViewport);
			optionParent = optionContent;
			scrollViewport.onResolved(function(geometry) {
				scrollController.updateMetrics(geometry.width, geometry.height,
					geometry.contentBounds.width, geometry.contentBounds.height);
			});
			dropdown.on(UiEventKind.Scroll, function(event) {
				if (!event.defaultPrevented && scrollController.scrollBy(0.0, event.deltaY))
					event.stopPropagation();
			});
			visibility.ensure = function(index:Int) {
				if (scrollController == null || scrollController.viewportHeight <= 0.0)
					return;
				var position = visiblePosition(index);
				if (position < 0)
					return;
				var optionTop = position * (rowHeight + 2.0);
				var optionBottom = optionTop + rowHeight;
				var target = scrollController.offsetY;
				if (optionTop < target)
					target = optionTop;
				else if (optionBottom > target + optionViewportHeight)
					target = optionBottom - optionViewportHeight;
				if (target != scrollController.offsetY) {
					scrollController.jumpTo(scrollController.offsetX, target);
				}
			};
		}
		for (position in 0...visibleIndices.length) {
			var originalIndex = visibleIndices[position];
			var option:SelectOption<Dynamic> = options[originalIndex];
			var optionStyle = new LayoutStyle();
			optionStyle.width = LayoutAxis.grow();
			optionStyle.height = LayoutAxis.fixed(rowHeight);
			optionStyle.padding = new Insets(9.0, 5.0, 9.0, 5.0);
			var optionButton = new Button(option.label, optionStyle,
				function() { onSelect(originalIndex); }, option.key);
			optionButton.enabled = isEnabled(originalIndex);
			optionButton.selected = sameValue(option.value, selectedValue);
			optionButton.semanticRole = AccessibilityRole.ListItem;
			optionButton.semanticActions = optionButton.enabled ? AccessibilityAction.Select : 0;
			var optionNode = context.withScope(new nativekit.ui.core.Key("option-" + option.key),
				function() return optionButton.build(context));
			var optionSemantics:Semantics = cast optionNode.semantics;
			optionSemantics.setSize = visibleIndices.length;
			optionSemantics.positionInSet = position + 1;
			optionNodes.push(optionNode);
			optionParent.add(optionNode);
		}

		for (position in 0...optionNodes.length) {
			var optionPosition = position;
			optionNodes[position].on(UiEventKind.KeyDown, function(event) {
				if (event.key == UiKey.Escape) {
					onClose();
					event.preventDefault();
					return;
				}
				var next = -1;
				if (event.key == UiKey.Home)
					next = firstEnabledVisible();
				else if (event.key == UiKey.End)
					next = lastEnabledVisible();
				else if (event.key == UiKey.Down || event.key == UiKey.Right)
					next = nextEnabled(visibleIndices[optionPosition], 1);
				else if (event.key == UiKey.Up || event.key == UiKey.Left)
					next = nextEnabled(visibleIndices[optionPosition], -1);
				if (next < 0)
					return;
				onActive(next);
				visibility.ensure(next);
				var nextPosition = visiblePosition(next);
				if (nextPosition >= 0 && nextPosition < optionNodes.length)
					context.requestFocus(optionNodes[nextPosition].id);
				event.preventDefault();
			});
		}
		root.add(dropdown);
		root.onPointerDownOutside(function(event) {
			if (event.button == 0)
				onClose();
		});
		return new SelectionPopupResult(dropdown, optionNodes, visibility.ensure);
	}

	function firstEnabledVisible():Int {
		for (index in visibleIndices)
			if (isEnabled(index))
				return index;
		return -1;
	}

	function lastEnabledVisible():Int {
		var position = visibleIndices.length - 1;
		while (position >= 0) {
			var index = visibleIndices[position];
			if (isEnabled(index))
				return index;
			position--;
		}
		return -1;
	}

	function visiblePosition(index:Int):Int {
		for (position in 0...visibleIndices.length)
			if (visibleIndices[position] == index)
				return position;
		return -1;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return Math.max(minimum, Math.min(maximum, value));

	static function sameValue(left:Dynamic, right:Dynamic):Bool
		return left == right;
}
