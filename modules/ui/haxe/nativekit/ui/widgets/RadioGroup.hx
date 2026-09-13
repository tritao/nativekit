package nativekit.ui.widgets;

import LayoutStyle;
import LayoutVisualKind;
import LayoutAxis;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.widgets.Radio;
import nativekit.ui.widgets.RadioOption;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Exclusive radio options with stable selected state and arrow-key traversal. */
class RadioGroup implements View {
	final key:Key;
	public final options:Array<RadioOption>;
	public var value:String;
	public final style:LayoutStyle;
	public var onChange:String->Void;
	public var hasChangeHandler(default, null):Bool;

	public function new(key:String, options:Array<RadioOption>, value:String,
			?onChange:String->Void, ?style:LayoutStyle) {
		this.key = new Key(key);
		this.options = options == null ? [] : options.copy();
		this.value = value == null ? "" : value;
		this.style = style == null ? defaultStyle() : style.copy();
		hasChangeHandler = onChange != null;
		this.onChange = onChange == null ? function(_) {} : onChange;
		var keys:Map<String, Bool> = new Map();
		var values:Map<String, Bool> = new Map();
		for (option in this.options) {
			if (option == null || keys.exists(option.key) || values.exists(option.value))
				throw "Radio option keys and values must be unique";
			keys.set(option.key, true);
			values.set(option.value, true);
		}
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var root = new RenderNode(context.id("radio-group"), LayoutVisualKind.Box, style.copy());
			root.semantics = new Semantics(AccessibilityRole.Group, "Radio group");
			var selected:State<String> = context.state(root.id, value);
			var active:String = cast selected.value;
			if (!hasEnabledValue(active))
				active = firstEnabledValue();
			if (active != cast selected.value)
				selected.update(active);
			value = active;

			var nodes:Array<RenderNode> = [];
			var select = function(next:String) {
				if (next == value)
					return;
				value = next;
				selected.update(next);
				if (hasChangeHandler)
					onChange(next);
			};
			for (option in options) {
				var radio = new Radio(option.key, option.label, option.value,
					option.value == active, function(next) { select(next); }, null, option.enabled);
				var node = radio.build(context);
				root.add(node);
				nodes.push(node);
			}
			for (index in 0...nodes.length) {
				var radioIndex = index;
				nodes[index].on(UiEventKind.KeyDown, function(event) {
					var direction = event.key == UiKey.Right || event.key == UiKey.Down ? 1 :
						event.key == UiKey.Left || event.key == UiKey.Up ? -1 : 0;
					if (direction == 0 || nodes.length == 0)
						return;
					var nextIndex = radioIndex;
					for (_ in 0...nodes.length) {
						nextIndex = (nextIndex + direction + nodes.length) % nodes.length;
						if (options[nextIndex].enabled)
							break;
					}
					if (options[nextIndex].enabled) {
						select(options[nextIndex].value);
						context.requestFocus(nodes[nextIndex].id);
						event.preventDefault();
					}
				});
			}
			return root;
		});
	}

	function hasEnabledValue(value:String):Bool {
		for (option in options)
			if (option.enabled && option.value == value)
				return true;
		return false;
	}

	function firstEnabledValue():String {
		for (option in options)
			if (option.enabled)
				return option.value;
		return "";
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.childGap = 2.0;
		return result;
	}
}
