package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutSizing;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.SelectionPopup;

/** Typed single-selection control with a positioned, keyboard-navigable option list. */
class Select<T> implements View {
	final key:Key;
	public final options:Array<SelectOption<T>>;
	public var value:T;
	public var enabled:Bool;
	public final style:LayoutStyle;
	public var onChange:T->Void;
	public var hasChangeHandler(default, null):Bool;

	public function new(key:String, options:Array<SelectOption<T>>, value:T,
			?onChange:T->Void, ?style:LayoutStyle) {
		if (key == null || key.length == 0)
			throw "Select requires a stable non-empty key";
		this.key = new Key(key);
		this.options = options == null ? [] : options.copy();
		this.value = value;
		hasChangeHandler = onChange != null;
		this.onChange = onChange == null ? function(_:T) {} : onChange;
		this.style = style == null ? defaultStyle() : style.copy();
		if (this.style.height.sizing == LayoutSizing.Fit)
			this.style.height = LayoutAxis.fixed(36.0);
		this.enabled = true;

		var keys:Map<String, Bool> = new Map();
		for (option in this.options) {
			if (option == null || keys.exists(option.key))
				throw "Select option keys must be unique";
			keys.set(option.key, true);
		}
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var rootStyle = style.copy();
			rootStyle.direction = LayoutDirection.TopToBottom;
			rootStyle.padding = new Insets(0.0, 0.0, 0.0, 0.0);
			rootStyle.childGap = 0.0;
			rootStyle.background = Color.rgba(0.0, 0.0, 0.0, 0.0);
			rootStyle.clipToParent = false;
			var root = new RenderNode(context.id("select"), LayoutVisualKind.Box, rootStyle);
			root.hitTestSelf = false;

			var rootXState:State<Float> = context.state(context.id("root-x"), 0.0);
			var rootYState:State<Float> = context.state(context.id("root-y"), 0.0);
			var triggerXState:State<Float> = context.state(context.id("trigger-x"), 0.0);
			var triggerYState:State<Float> = context.state(context.id("trigger-y"), 0.0);
			var triggerWidthState:State<Float> = context.state(context.id("trigger-width"),
				triggerHeightFallback());
			var triggerHeightState:State<Float> = context.state(context.id("trigger-height"),
				triggerHeightFallback());
			var anchorKnownState:State<Bool> = context.state(context.id("anchor-known"), false);
			var rememberFloat = function(state:State<Float>, value:Float) {
				var current:Float = state.value;
				if (!finite(current) || Math.abs(current - value) > 0.001)
					state.update(value);
			};
			root.onResolved(function(geometry) {
				if (finite(geometry.x) && finite(geometry.y)) {
					rememberFloat(rootXState, geometry.x);
					rememberFloat(rootYState, geometry.y);
				}
			});

			var selectedState:State<T> = context.state(context.id("value"), value);
			var storedValue:T = selectedState.value;
			if (!sameValue(storedValue, value))
				selectedState.update(value);
			var selectedIndex = enabledValueIndex(value);
			if (selectedIndex < 0)
				selectedIndex = firstEnabledIndex();
			if (selectedIndex >= 0 && !sameValue(value, options[selectedIndex].value)) {
				value = options[selectedIndex].value;
				selectedState.update(value);
			}

			var openState:State<Bool> = context.state(context.id("open"), false);
			var isOpen:Bool = openState.value;
			var firstEnabled = firstEnabledIndex();
			if (!enabled || firstEnabled < 0) {
				if (isOpen)
					openState.update(false);
				isOpen = false;
			}
			root.layout.style.zIndex = isOpen ? 10 : 0;

			var initialActive = selectedIndex >= 0 ? selectedIndex : firstEnabled;
			var activeState:State<Int> = context.state(context.id("active"), initialActive);
			var active:Int = activeState.value;
			if (!isEnabledIndex(active))
				active = initialActive;
			var storedActive:Int = activeState.value;
			if (active != storedActive)
				activeState.update(active);

			var triggerNode:RenderNode = null;
			var optionNodes:Array<RenderNode> = [];
			var popupVisibility = new SelectionPopupVisibility();
			var close = function() {
				if (openState.value == true)
					openState.update(false);
				if (triggerNode != null)
					context.requestFocus(triggerNode.id);
			};
			var setActive = function(index:Int) {
				var current:Int = activeState.value;
				if (isEnabledIndex(index)) {
					if (index != current)
						activeState.update(index);
					popupVisibility.ensure(index);
				}
			};
			var focusOption = function(index:Int) {
				if (index >= 0 && index < optionNodes.length)
					context.requestFocus(optionNodes[index].id);
			};
			var nextEnabled = function(index:Int, direction:Int) {
				if (options.length == 0)
					return -1;
				var candidate = index;
				for (_ in 0...options.length) {
					candidate = (candidate + direction + options.length) % options.length;
					if (isEnabledIndex(candidate))
						return candidate;
				}
				return index;
			};
			var selectOption = function(index:Int) {
				if (!enabled || !isEnabledIndex(index))
					return;
				var option = options[index];
				var changed = !sameValue(value, option.value);
				value = option.value;
				selectedState.update(value);
				setActive(index);
				close();
				if (changed && hasChangeHandler)
					onChange(value);
			};
			var toggleOpen = function() {
				if (!enabled || firstEnabled < 0)
					return;
				if (openState.value == true)
					close();
				else {
					openState.update(true);
					setActive(selectedIndex >= 0 ? selectedIndex : firstEnabled);
				}
			};

			var selectedLabel = selectedIndex >= 0 ? options[selectedIndex].label : "Select";
			if (selectedLabel.length == 0)
				selectedLabel = "Select";
			var triggerStyle = style.copy();
			triggerStyle.width = LayoutAxis.grow();
			var trigger = new Button(selectedLabel, triggerStyle, toggleOpen, "trigger");
			trigger.enabled = enabled && firstEnabled >= 0;
			trigger.semanticRole = AccessibilityRole.ComboBox;
			trigger.semanticActions = AccessibilityAction.Activate | AccessibilityAction.SetValue |
				(isOpen ? AccessibilityAction.Collapse : AccessibilityAction.Expand);
			triggerNode = context.withScope(new Key("trigger"), function() return trigger.build(context));
			var triggerSemantics:Semantics = cast triggerNode.semantics;
			triggerSemantics.label = selectedLabel;
			triggerSemantics.value = selectedLabel;
			triggerSemantics.states |= AccessibilityState.HasPopup;
			if (isOpen)
				triggerSemantics.states |= AccessibilityState.Expanded;
			triggerNode.on(UiEventKind.KeyDown, function(event) {
				if (!enabled || firstEnabled < 0)
					return;
				if (event.key == UiKey.Escape && openState.value == true) {
					close();
					event.preventDefault();
					return;
				}
				if (event.key != UiKey.Down && event.key != UiKey.Up &&
					event.key != UiKey.Home && event.key != UiKey.End)
					return;
				var wasOpen:Bool = openState.value;
				if (!wasOpen)
					openState.update(true);
				var base = selectedIndex >= 0 ? selectedIndex : firstEnabled;
				var next:Int = base;
				if (event.key == UiKey.Home)
					next = firstEnabled;
				else if (event.key == UiKey.End)
					next = lastEnabledIndex();
				else if (wasOpen)
					next = nextEnabled(activeState.value,
						event.key == UiKey.Down ? 1 : -1);
				setActive(next);
				if (wasOpen)
					focusOption(next);
				event.preventDefault();
			});
			triggerNode.on(UiEventKind.AccessibilitySetValue, function(event) {
				if (!enabled || event.text == null)
					return;
				for (index in 0...options.length)
					if (options[index].enabled && (options[index].key == event.text ||
						options[index].label == event.text)) {
						selectOption(index);
						event.preventDefault();
						return;
					}
			});
			root.add(triggerNode);
			triggerNode.onResolved(function(geometry) {
				if (finite(geometry.x) && finite(geometry.y) && finite(geometry.width) &&
					finite(geometry.height) && geometry.width >= 0.0 && geometry.height >= 0.0) {
					rememberFloat(triggerXState, geometry.x);
					rememberFloat(triggerYState, geometry.y);
					rememberFloat(triggerWidthState, geometry.width);
					rememberFloat(triggerHeightState, geometry.height);
					if (anchorKnownState.value != true)
						anchorKnownState.update(true);
				}
			});

			root.focusTrap = isOpen;
			if (isOpen) {
				var anchorKnown:Bool = anchorKnownState.value;
				var popupRootX:Float = rootXState.value;
				var popupRootY:Float = rootYState.value;
				var popupTriggerX:Float = triggerXState.value;
				var popupTriggerY:Float = triggerYState.value;
				var popupTriggerWidth:Float = triggerWidthState.value;
				var popupTriggerHeight:Float = triggerHeightState.value;
				if (!anchorKnown) {
					popupRootX = 0.0;
					popupRootY = 0.0;
					popupTriggerX = 0.0;
					popupTriggerY = 0.0;
					popupTriggerWidth = fallbackWidth();
					popupTriggerHeight = triggerHeightFallback();
				}
				var visibleIndices:Array<Int> = [];
				for (index in 0...options.length)
					visibleIndices.push(index);
				var popupOptions:Array<SelectOption<Dynamic>> = [];
				for (option in options)
					popupOptions.push(cast option);
				var popup = new SelectionPopup(popupOptions, visibleIndices, value, active,
					popupRootX, popupRootY, popupTriggerX, popupTriggerY,
					popupTriggerWidth, popupTriggerHeight, fallbackWidth(),
					triggerHeightFallback(),
					function(index:Int) { selectOption(index); }, close,
					function(index:Int) { setActive(index); },
					function(index:Int) return isEnabledIndex(index),
					function(index:Int, direction:Int) return nextEnabled(index, direction));
				var popupResult = popup.build(context, root);
				optionNodes = popupResult.optionNodes;
				popupVisibility.ensure = popupResult.ensureActiveVisible;
			}
			root.on(UiEventKind.KeyDown, function(event) {
				if (event.key == UiKey.Escape && openState.value == true) {
					close();
					event.preventDefault();
				}
			});
			return root;
		});
	}

	function enabledValueIndex(value:T):Int {
		for (index in 0...options.length)
			if (options[index].enabled && sameValue(options[index].value, value))
				return index;
		return -1;
	}

	function firstEnabledIndex():Int {
		for (index in 0...options.length)
			if (options[index].enabled)
				return index;
		return -1;
	}

	function lastEnabledIndex():Int {
		var index = options.length - 1;
		while (index >= 0) {
			if (options[index].enabled)
				return index;
			index--;
		}
		return -1;
	}

	function isEnabledIndex(index:Int):Bool
		return index >= 0 && index < options.length && options[index].enabled;

	function triggerHeightFallback():Float
		return style.height.sizing == LayoutSizing.Fixed && style.height.value > 0.0
			? style.height.value : 36.0;

	function fallbackWidth():Float
		return style.width.sizing == LayoutSizing.Fixed && style.width.value > 0.0
			? style.width.value : 220.0;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(220.0);
		result.height = LayoutAxis.fixed(36.0);
		result.padding = new Insets(12.0, 9.0, 12.0, 9.0);
		result.background = Color.rgba(0.16, 0.4, 0.78, 1.0);
		result.radiusTopLeft = result.radiusTopRight = 5.0;
		result.radiusBottomLeft = result.radiusBottomRight = 5.0;
		return result;
	}

	static function sameValue(left:Dynamic, right:Dynamic):Bool
		return left == right;
}
