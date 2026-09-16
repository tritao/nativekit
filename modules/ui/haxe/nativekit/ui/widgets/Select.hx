package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutPositioning;
import LayoutSizing;
import LayoutStyle;
import LayoutVisualKind;
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

			var selectedState:State<Dynamic> = context.state(context.id("value"), value);
			var storedValue:Dynamic = selectedState.value;
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
			var isOpen:Bool = cast openState.value;
			var firstEnabled = firstEnabledIndex();
			if (!enabled || firstEnabled < 0) {
				if (isOpen)
					openState.update(false);
				isOpen = false;
			}

			var initialActive = selectedIndex >= 0 ? selectedIndex : firstEnabled;
			var activeState:State<Int> = context.state(context.id("active"), initialActive);
			var active:Int = cast activeState.value;
			if (!isEnabledIndex(active))
				active = initialActive;
			var storedActive:Int = cast activeState.value;
			if (active != storedActive)
				activeState.update(active);

			var triggerNode:RenderNode = null;
			var optionNodes:Array<RenderNode> = [];
			var close = function() {
				if (openState.value == true)
					openState.update(false);
				if (triggerNode != null)
					context.requestFocus(triggerNode.id);
			};
			var setActive = function(index:Int) {
				var current:Int = cast activeState.value;
				if (isEnabledIndex(index) && index != current)
					activeState.update(index);
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
				var wasOpen:Bool = cast openState.value;
				if (!wasOpen)
					openState.update(true);
				var base = selectedIndex >= 0 ? selectedIndex : firstEnabled;
				var next:Int = base;
				if (event.key == UiKey.Home)
					next = firstEnabled;
				else if (event.key == UiKey.End)
					next = lastEnabledIndex();
				else if (wasOpen)
					next = nextEnabled(cast activeState.value, event.key == UiKey.Down ? 1 : -1);
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

			root.focusTrap = isOpen;
			if (isOpen) {
				var dropdownStyle = new LayoutStyle();
				dropdownStyle.width = LayoutAxis.grow();
				dropdownStyle.height = LayoutAxis.fit();
				dropdownStyle.direction = LayoutDirection.TopToBottom;
				dropdownStyle.positioning = LayoutPositioning.Absolute;
				dropdownStyle.positionY = triggerHeight();
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
				dropdown.semantics = listSemantics;
				for (index in 0...options.length) {
					var optionIndex = index;
					var option = options[index];
					var optionStyle = new LayoutStyle();
					optionStyle.width = LayoutAxis.grow();
					optionStyle.height = LayoutAxis.fixed(32.0);
					optionStyle.padding = new Insets(9.0, 5.0, 9.0, 5.0);
					var optionButton = new Button(option.label, optionStyle,
						function() { selectOption(optionIndex); }, option.key);
					optionButton.enabled = enabled && option.enabled;
					optionButton.selected = sameValue(option.value, value);
					optionButton.semanticRole = AccessibilityRole.ListItem;
					optionButton.semanticActions = option.enabled
						? AccessibilityAction.Select : 0;
					var optionNode = context.withScope(new Key("option-" + option.key),
						function() return optionButton.build(context));
					var optionSemantics:Semantics = cast optionNode.semantics;
					optionSemantics.setSize = options.length;
					optionSemantics.positionInSet = index + 1;
					optionNodes.push(optionNode);
					dropdown.add(optionNode);
				}
				for (index in 0...optionNodes.length) {
					var optionIndex = index;
					optionNodes[index].on(UiEventKind.KeyDown, function(event) {
						if (event.key == UiKey.Escape) {
							close();
							event.preventDefault();
							return;
						}
						var next:Int = -1;
						if (event.key == UiKey.Home)
							next = firstEnabledIndex();
						else if (event.key == UiKey.End)
							next = lastEnabledIndex();
						else if (event.key == UiKey.Down || event.key == UiKey.Right)
							next = nextEnabled(optionIndex, 1);
						else if (event.key == UiKey.Up || event.key == UiKey.Left)
							next = nextEnabled(optionIndex, -1);
						if (next < 0)
							return;
						setActive(next);
						focusOption(next);
						event.preventDefault();
					});
				}
				root.add(dropdown);
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

	function triggerHeight():Float
		return style.height.sizing == LayoutSizing.Fixed && style.height.value > 0.0
			? style.height.value : 36.0;

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

	function sameValue(left:Dynamic, right:Dynamic):Bool
		return left == right;
}
