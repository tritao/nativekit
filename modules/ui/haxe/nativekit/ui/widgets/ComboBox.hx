package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutPositioning;
import LayoutSizing;
import LayoutStyle;
import LayoutVisualKind;
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
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Typed editable selection control with filtered, keyboard-navigable options. */
class ComboBox < T > implements View {
	final key:Key;
	public final options:Array<SelectOption < T>>;
	public var value:T;
	public var enabled:Bool;
	public final style:LayoutStyle;
	public var placeholder:String;
	public var onChange:T -> Void;
	public var onQueryChange:String -> Void;
	public var hasChangeHandler(default, null):Bool;
	public var hasQueryChangeHandler(default, null):Bool;

	public function new(
		key:String,
		options:Array<SelectOption < T>>,
		value:T,
		? onChange:T -> Void,
		? style:LayoutStyle,
		? placeholder:String,
		? onQueryChange:String -> Void
	) {
		if (key == null || key.length == 0) throw "ComboBox requires a stable non-empty key";
		this.key = new Key(key);
		this.options = options == null ?[] : options.copy();
		var keys:Map<String, Bool> = new Map();
		for (option in this.options) {
			if (option == null || keys.exists(option.key)) throw "ComboBox option keys must be unique";
			keys.set(option.key, true);
		}
		this.value = value;
		hasChangeHandler = onChange != null;
		this.onChange = onChange == null ? function(_ : T) {
		}
		:onChange;
		this.style = style == null ? defaultStyle() : style.copy();
		if (this.style.height.sizing == LayoutSizing.Fit) this.style.height = LayoutAxis.fixed(40.0);
		this.placeholder = placeholder == null || placeholder.length == 0 ? "Search" : placeholder;
		this.onQueryChange = onQueryChange == null ? function(_ : String) {
		}
		:onQueryChange;
		hasQueryChangeHandler = onQueryChange != null;
		enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var rootStyle = style.copy();
			rootStyle.direction = LayoutDirection.TopToBottom;
			rootStyle.padding = new Insets(0.0, 0.0, 0.0, 0.0);
			rootStyle.childGap = 0.0;
			rootStyle.background = Color.rgba(0.0, 0.0, 0.0, 0.0);
			rootStyle.clipToParent = false;
			var root = new RenderNode(context.id("combo-box"), LayoutVisualKind.Box, rootStyle);
			root.hitTestSelf = false;

			var rootXState:State<Float> = context.state(context.id("root-x"), 0.0);
			var rootYState:State<Float> = context.state(context.id("root-y"), 0.0);
			var triggerXState:State<Float> = context.state(context.id("trigger-x"), 0.0);
			var triggerYState:State<Float> = context.state(context.id("trigger-y"), 0.0);
			var triggerWidthState:State<Float> = context.state(context.id("trigger-width"), triggerHeightFallback());
			var triggerHeightState:State<Float> = context.state(context.id("trigger-height"), triggerHeightFallback());
			var anchorKnownState:State<Bool> = context.state(context.id("anchor-known"), false);
			var rememberFloat = function(state:State<Float>, value:Float) {
				var current:Float = cast state.value;
				if (!finite(current) || Math.abs(current - value) > 0.001) state.update(value);
			};
			root.onResolved(function(geometry) {
				if (finite(geometry.x) && finite(geometry.y)) {
					rememberFloat(rootXState, geometry.x);
					rememberFloat(rootYState, geometry.y);
				}
			}
			);

			var selectedState:State<Dynamic> = context.state(context.id("value"), value);
			var storedValue:Dynamic = selectedState.value;
			if (!sameValue(storedValue, value)) selectedState.update(value);
			var selectedIndex = enabledValueIndex(value);
			if (selectedIndex < 0) selectedIndex = firstEnabledIndex();
			if (selectedIndex >= 0 && !sameValue(value, options[selectedIndex].value)) {
				value = options[selectedIndex].value;
				selectedState.update(value);
			}
			var selectedLabel = selectedIndex >= 0 ? options[selectedIndex].label : "";
			var labelState:State<String> = context.state(context.id("value-label"), selectedLabel);
			var queryState:State<String> = context.state(context.id("query"), selectedLabel);
			var storedLabel:String = cast labelState.value;
			var query:String = cast queryState.value;
			if (storedLabel != selectedLabel && query == storedLabel) {
				query = selectedLabel;
				queryState.update(query);
			}
			if (storedLabel != selectedLabel) labelState.update(selectedLabel);

			var filteredIndices:Array<Int> = [];
			var loweredQuery = query == selectedLabel ? "" : query.toLowerCase();
			for (index in 0...options.length) {
				var label = options[index].label.toLowerCase();
				if (loweredQuery.length == 0 || label.indexOf(loweredQuery) >= 0) filteredIndices.push(index);
			}

			var openState:State<Bool> = context.state(context.id("open"), false);
			var isOpen:Bool = cast openState.value;
			if (!enabled || filteredIndices.length == 0) {
				if (isOpen) openState.update(false);
				isOpen = false;
			}
			var initialActive = filteredIndices.length == 0 ? -1 : filteredIndices[0];
			if (selectedIndex >= 0 && contains(filteredIndices, selectedIndex)) initialActive = selectedIndex;
			var activeState:State<Int> = context.state(context.id("active"), initialActive);
			var active:Int = cast activeState.value;
			if (!contains(filteredIndices, active)) active = initialActive;
			var storedActive:Int = cast activeState.value;
			if (active != storedActive) activeState.update(active);

			var inputNode:RenderNode = null;
			var optionNodes:Array<RenderNode> = [];
			var scrollController:ScrollController = null;
			var ensureActiveVisible = function(_:Int) {
			};
			var close = function() {
				if (openState.value == true) openState.update(false);
				if (inputNode != null) context.requestFocus(inputNode.id);
			};
			var setActive = function(index:Int) {
				if (contains(filteredIndices, index)) {
					var current:Int = cast activeState.value;
					if (index != current) activeState.update(index);
					ensureActiveVisible(index);
				}
			};
			var focusOption = function(index:Int) {
				if (index >= 0 && index < optionNodes.length) context.requestFocus(optionNodes[index].id);
			};
			var selectOption = function(index:Int) {
				if (!enabled || !isEnabledIndex(index)) return;
				var option = options[index];
				var changed = !sameValue(value, option.value);
				value = option.value;
				selectedState.update(value);
				query = option.label;
				queryState.update(query);
				labelState.update(query);
				setActive(index);
				close();
				if (changed && hasChangeHandler) onChange(value);
			};
			var open = function() {
				if (!enabled || filteredIndices.length == 0) return;
				if (openState.value != true) openState.update(true);
				setActive(contains(filteredIndices, selectedIndex) ? selectedIndex : filteredIndices[0]);
			};
			var submitActive = function() {
				var submittedIndex:Int = cast activeState.value;
				if (contains(filteredIndices, submittedIndex)) selectOption(submittedIndex);
				else close();
			};

			var inputStyle = style.copy();
			inputStyle.width = LayoutAxis.grow();
			var input = new TextField("input", query, function(next) {
				query = next == null ? "" : next;
				queryState.update(query);
				if (hasQueryChangeHandler) onQueryChange(query);
				open();
			}, inputStyle, placeholder);
			input.enabled = enabled;
			input.semanticRole = AccessibilityRole.ComboBox;
			input.semanticActions = AccessibilityAction.SetValue | AccessibilityAction.SetSelection |(isOpen
				? AccessibilityAction.Collapse : AccessibilityAction.Expand);
			input.onSubmit = function(_) {
				if (openState.value != true) open();
				else submitActive();
			};
			inputNode = context.withScope(new Key("input"), function() return input.build(context));
			var inputSemantics:Semantics = cast inputNode.semantics;
			inputSemantics.states |= AccessibilityState.HasPopup;
			if (isOpen) inputSemantics.states |= AccessibilityState.Expanded;
			inputNode.on(UiEventKind.Click, function(_) {
				open();
			}
			);
			inputNode.on(UiEventKind.Activate, function(event) {
				if (!enabled) return;
				if (openState.value != true) open();
				else submitActive();
				event.preventDefault();
			}
			);
			inputNode.on(UiEventKind.AccessibilitySetValue, function(event) {
				if (!enabled || event.text == null) return;
				for (index in 0...options.length) if (options[index].enabled
					&&(options[index].key == event.text || options[index].label == event.text)) {
					selectOption(index);
					event.preventDefault();
					return;
				}
			}
			);
			inputNode.on(UiEventKind.KeyDown, function(event) {
				if (!enabled) return;
				if (event.key == UiKey.Escape && openState.value == true) {
					close();
					event.preventDefault();
					return;
				}
				if (event.key != UiKey.Down
					&& event.key != UiKey.Up && event.key != UiKey.Home && event.key != UiKey.End) return;
				if (filteredIndices.length == 0) return;
				var wasOpen:Bool = cast openState.value;
				if (!wasOpen) open();
				var next:Int = cast activeState.value;
				if (event.key == UiKey.Home) next = filteredIndices[0];
				else if (event.key == UiKey.End) next = filteredIndices[filteredIndices.length - 1];
				else if (wasOpen) {
					var currentActive:Int = cast activeState.value;
					next = nextEnabled(filteredIndices, currentActive, event.key == UiKey.Down ? 1 : -1);
				}
				setActive(next);
				if (wasOpen) focusOption(filteredPosition(filteredIndices, next));
				event.preventDefault();
			}
			);
			root.add(inputNode);
			inputNode.onResolved(function(geometry) {
				if (finite(geometry.x) && finite(geometry.y)
					&& finite(geometry.width) && finite(geometry.height) && geometry.width >= 0.0 && geometry.height >= 0.0) {
					rememberFloat(triggerXState, geometry.x);
					rememberFloat(triggerYState, geometry.y);
					rememberFloat(triggerWidthState, geometry.width);
					rememberFloat(triggerHeightState, geometry.height);
					if (anchorKnownState.value != true) anchorKnownState.update(true);
				}
			}
			);

			root.focusTrap = isOpen;
			if (isOpen && filteredIndices.length > 0) {
				var anchorKnown:Bool = cast anchorKnownState.value;
				var rootX:Float = cast rootXState.value;
				var rootY:Float = cast rootYState.value;
				var triggerX:Float = cast triggerXState.value;
				var triggerY:Float = cast triggerYState.value;
				var triggerWidth:Float = cast triggerWidthState.value;
				var triggerHeight:Float = cast triggerHeightState.value;
				if (!anchorKnown) {
					rootX = 0.0;
					rootY = 0.0;
					triggerX = 0.0;
					triggerY = 0.0;
					triggerWidth = fallbackWidth();
					triggerHeight = triggerHeightFallback();
				}
				var edge = Math.min(8.0, context.viewportWidth * 0.5);
				var availableWidth = Math.max(1.0, context.viewportWidth - edge * 2.0);
				var popupWidth = Math.min(Math.max(1.0, triggerWidth), availableWidth);
				var popupX = clamp(triggerX, edge, Math.max(edge, context.viewportWidth - edge - popupWidth));
				var rowHeight = 32.0;
				var popupPadding = 8.0;
				var popupGap = filteredIndices.length > 0 ?(filteredIndices.length - 1) * 2.0 : 0.0;
				var naturalHeight = popupPadding + filteredIndices.length * rowHeight + popupGap;
				var placementGap = 4.0;
				var below = Math.max(0.0, context.viewportHeight -(triggerY + triggerHeight) - placementGap);
				var above = Math.max(0.0, triggerY - placementGap);
				var opensBelow = below >= naturalHeight || below >= above;
				var availableHeight = opensBelow ? below : above;
				var popupHeight = Math.min(Math.max(1.0, naturalHeight), Math.max(1.0, availableHeight));
				var popupGlobalY = opensBelow ? triggerY + triggerHeight + placementGap : triggerY - popupHeight - placementGap;
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
				if (needsScroll) listSemantics.actions = AccessibilityAction.ScrollForward | AccessibilityAction.ScrollBackward;
				dropdown.semantics = listSemantics;
				var optionParent = dropdown;
				if (needsScroll) {
					var viewportStyle = new LayoutStyle();
					viewportStyle.width = LayoutAxis.grow();
					viewportStyle.height = LayoutAxis.fixed(optionViewportHeight);
					viewportStyle.clipVertical = true;
					var scrollViewport = new RenderNode(context.id("option-viewport"), LayoutVisualKind.Box, viewportStyle);
					var storedScroll:State<ScrollController> = context.state(scrollViewport.id, new ScrollController());
					scrollController = cast storedScroll.value;
					scrollController.bind(function(value) {
						storedScroll.update(value);
					}
					);
					var contentStyle = new LayoutStyle();
					contentStyle.width = LayoutAxis.grow();
					contentStyle.height = LayoutAxis.fit();
					contentStyle.direction = LayoutDirection.TopToBottom;
					contentStyle.transform = Transform2D.identity().translated(0.0, -scrollController.offsetY);
					var optionContent = new RenderNode(context.id("option-content"), LayoutVisualKind.Box, contentStyle);
					scrollViewport.add(optionContent);
					dropdown.add(scrollViewport);
					optionParent = optionContent;
					scrollViewport.onResolved(function(geometry) {
						scrollController.updateMetrics(
							geometry.width,
							geometry.height,
							geometry.contentBounds.width,
							geometry.contentBounds.height
						);
					}
					);
				}
				if (needsScroll) dropdown.on(UiEventKind.Scroll, function(event) {
					if (!event.defaultPrevented && scrollController.scrollBy(0.0, -event.deltaY)) event.stopPropagation();
				}
				);
				if (needsScroll) ensureActiveVisible = function(index:Int) {
					if (scrollController == null || scrollController.viewportHeight <= 0.0) return;
					var position = filteredPosition(filteredIndices, index);
					if (position < 0) return;
					var optionTop = position *(rowHeight + 2.0);
					var optionBottom = optionTop + rowHeight;
					var target = scrollController.offsetY;
					if (optionTop < target) target = optionTop;
					else if (optionBottom > target + optionViewportHeight) target = optionBottom - optionViewportHeight;
					if (target != scrollController.offsetY) scrollController.jumpTo(scrollController.offsetX, target);
				};
				ensureActiveVisible(active);
				for (position in 0...filteredIndices.length) {
					var originalIndex = filteredIndices[position];
					var option = options[originalIndex];
					var optionStyle = new LayoutStyle();
					optionStyle.width = LayoutAxis.grow();
					optionStyle.height = LayoutAxis.fixed(rowHeight);
					optionStyle.padding = new Insets(9.0, 5.0, 9.0, 5.0);
					var optionButton = new Button(option.label, optionStyle, function() {
						selectOption(originalIndex);
					}, option.key);
					optionButton.enabled = enabled && option.enabled;
					optionButton.selected = sameValue(option.value, value);
					optionButton.semanticRole = AccessibilityRole.ListItem;
					optionButton.semanticActions = option.enabled ? AccessibilityAction.Select : 0;
					var optionNode = context.withScope(
						new Key("option-" + option.key),
						function() return optionButton.build(context)
					);
					var optionSemantics:Semantics = cast optionNode.semantics;
					optionSemantics.setSize = filteredIndices.length;
					optionSemantics.positionInSet = position + 1;
					optionNodes.push(optionNode);
					optionParent.add(optionNode);
				}
				for (position in 0...optionNodes.length) {
					var optionPosition = position;
					optionNodes[position].on(UiEventKind.KeyDown, function(event) {
						if (event.key == UiKey.Escape) {
							close();
							event.preventDefault();
							return;
						}
						var next:Int = -1;
						if (event.key == UiKey.Home) next = filteredIndices[0];
						else if (event.key == UiKey.End) next = filteredIndices[filteredIndices.length - 1];
						else if (event.key == UiKey.Down || event.key == UiKey.Right) next = nextEnabled(
							filteredIndices,
							filteredIndices[optionPosition],
							1
						);
						else if (event.key == UiKey.Up || event.key == UiKey.Left) next = nextEnabled(
							filteredIndices,
							filteredIndices[optionPosition],
							-1
						);
						if (next < 0) return;
						setActive(next);
						focusOption(filteredPosition(filteredIndices, next));
						event.preventDefault();
					}
					);
				}
				root.add(dropdown);
				root.onPointerDownOutside(function(event) {
					if (event.button == 0) close();
				}
				);
			}
			root.on(UiEventKind.KeyDown, function(event) {
				if (event.key == UiKey.Escape && openState.value == true) {
					close();
					event.preventDefault();
				}
			}
			);
			return root;
		}
		);
	}

	function enabledValueIndex(value:T):Int {
		for (index in 0...options.length) if (options[index].enabled && sameValue(options[index].value,
			value)) return index;
		return - 1;
	}

	function firstEnabledIndex():Int {
		for (index in 0...options.length) if (options[index].enabled) return index;
		return - 1;
	}

	function isEnabledIndex(index:Int):Bool return index >= 0 && index < options.length && options[index].enabled;

	function filteredPosition(filteredIndices:Array<Int>, index:Int):Int {
		for (position in 0...filteredIndices.length) if (filteredIndices[position] == index) return position;
		return - 1;
	}

	function nextEnabled(filteredIndices:Array<Int>, index:Int, direction:Int):Int {
		var position = filteredPosition(filteredIndices, index);
		if (position < 0 || filteredIndices.length == 0) return - 1;
		for (_ in 0...filteredIndices.length) {
			position =(position + direction + filteredIndices.length) % filteredIndices.length;
			if (isEnabledIndex(filteredIndices[position])) return filteredIndices[position];
		}
		return index;
	}

	function triggerHeightFallback():Float return style.height.sizing == LayoutSizing.Fixed
		&& style.height.value > 0.0 ? style.height.value : 40.0;

	function fallbackWidth():Float return style.width.sizing == LayoutSizing.Fixed
		&& style.width.value > 0.0 ? style.width.value : 240.0;

	static function contains(values:Array<Int>, value:Int):Bool {
		for (entry in values) if (entry == value) return true;
		return false;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float return Math.max(
		minimum,
		Math.min(
			maximum,
			value
		)
	);

	static inline function finite(value:Float):Bool return value == value && value - value == 0.0;

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(240.0);
		result.height = LayoutAxis.fixed(40.0);
		result.padding = new Insets(10.0, 8.0, 10.0, 8.0);
		result.background = Color.rgba(0.11, 0.13, 0.17, 1.0);
		result.radiusTopLeft = result.radiusTopRight = 5.0;
		result.radiusBottomLeft = result.radiusBottomRight = 5.0;
		result.clipHorizontal = true;
		return result;
	}

	static function sameValue(left:Dynamic, right:Dynamic):Bool return left == right;
}
