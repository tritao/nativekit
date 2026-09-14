package inspector;

import Rect;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityState;

/** API synopsis and canonical usage examples keyed by semantic widget role. */
class WidgetDocsRegistry {
	public static function describe(role:Int):{description:String, behavior:String, code:String} {
		return switch role {
			case 1: {
				description: "A compositional action control. Haxe owns its interaction state and semantic action; the render tree contains a box and label.",
				behavior: "Activate with click, Enter or Space. It participates in keyboard focus and exposes an Activate accessibility action.",
				code: 'new Button("Save changes", style, onActivate, "save")'
			};
			case 2: {
				description: "A boolean selection control composed from ordinary Haxe layout and paint nodes.",
				behavior: "Click or press Space to toggle. Checked state is reflected in the semantic value and state bits.",
				code: 'new Checkbox("show-labels", "Show labels", checked, onChange)'
			};
			case 3: {
				description: "One option in an exclusive radio selection group.",
				behavior: "Arrow keys move between enabled options; Space or Enter selects the focused option.",
				code: 'new RadioGroup("density", options, selected, onChange)'
			};
			case 5: {
				description: "An editable text control backed by framework editor state and NativeKit text-input services.",
				behavior: "Pointer hit testing positions the caret; keyboard selection/editing and platform clipboard and IME are supported where available.",
				code: 'new TextField("name", value, onChange, style, "Display name")'
			};
			case 11: {
				description: "A continuous range control with pointer, keyboard and semantic value support.",
				behavior: "Drag the thumb or track; arrow keys and accessibility increment/decrement adjust the snapped value.",
				code: 'new Slider("volume", "Volume", value, 0, 1, 0.01, onChange)'
			};
			case 12: {
				description: "A clipped viewport that translates persistent content using Haxe-owned scroll state.",
				behavior: "Wheel, touch/drag, and semantic forward/back actions update the scroll controller.",
				code: 'new ScrollView("results", content, style, ScrollAxis.Vertical)'
			};
			case 9, 10: {
				description: "A list container or a realized item within a data-oriented view.",
				behavior: "The virtual list builds only visible fixed-height rows plus a small overscan window.",
				code: 'new VirtualList("rows", count, rowHeight, buildRow, style)'
			};
			case 7: {
				description: "A rendered image with an optional accessible name.",
				behavior: "The image participates in normal layout, clipping and render order.",
				code: 'new ImageView("avatar", image, "Profile photo")'
			};
			case 8: {
				description: "A heading that identifies a section of the current demo.",
				behavior: "Headings expose a navigable semantic role while rendering with the shared text pipeline.",
				code: 'new Text("Section title", headingStyle)'
			};
			case 0: {
				description: "A layout or grouping node in the Haxe-owned render tree.",
				behavior: "It composes children and contributes resolved bounds, clipping and z-order without adding a native widget abstraction.",
				code: 'new Column("settings", [keyed("name", nameField)])'
			};
			default: {
				description: "A render primitive produced by a Haxe widget or layout composition.",
				behavior: "Geometry and clipping are resolved natively; events and semantic identity remain connected to this Haxe node.",
				code: 'new Text("Hello, NativeKit UI")'
			};
		};
	}

	public static function roleName(role:Int):String {
		return switch role {
			case 0: "Group";
			case 1: "Button";
			case 2: "Checkbox";
			case 3: "Radio";
			case 5: "TextField";
			case 6: "Link";
			case 7: "Image";
			case 8: "Heading";
			case 9: "List";
			case 10: "ListItem";
			case 11: "Slider";
			case 12: "ScrollArea";
			default: "Text";
		};
	}

	public static function visualName(kind:Int):String {
		return switch kind {
			case 1: "Box";
			case 2: "Text";
			case 3: "Image";
			case 4: "Custom";
			default: "Unknown";
		};
	}

	public static function semanticStateNames(states:Int):String {
		var values:Array<String> = [];
		if ((states & AccessibilityState.Focusable) != 0) values.push("Focusable");
		if ((states & AccessibilityState.Focused) != 0) values.push("Focused");
		if ((states & AccessibilityState.Selected) != 0) values.push("Selected");
		if ((states & AccessibilityState.Checked) != 0) values.push("Checked");
		if ((states & AccessibilityState.Disabled) != 0) values.push("Disabled");
		if ((states & AccessibilityState.ReadOnly) != 0) values.push("ReadOnly");
		if ((states & AccessibilityState.Multiline) != 0) values.push("Multiline");
		if ((states & AccessibilityState.Password) != 0) values.push("Password");
		if ((states & AccessibilityState.Expanded) != 0) values.push("Expanded");
		return values.length == 0 ? "(none)" : values.join(", ");
	}

	public static function actionNames(actions:Int):String {
		var values:Array<String> = [];
		if ((actions & AccessibilityAction.Activate) != 0) values.push("Activate");
		if ((actions & AccessibilityAction.Focus) != 0) values.push("Focus");
		if ((actions & AccessibilityAction.SetValue) != 0) values.push("SetValue");
		if ((actions & AccessibilityAction.SetSelection) != 0) values.push("SetSelection");
		if ((actions & AccessibilityAction.Increment) != 0) values.push("Increment");
		if ((actions & AccessibilityAction.Decrement) != 0) values.push("Decrement");
		if ((actions & AccessibilityAction.ScrollForward) != 0) values.push("ScrollForward");
		if ((actions & AccessibilityAction.ScrollBackward) != 0) values.push("ScrollBackward");
		if ((actions & AccessibilityAction.MoveNext) != 0) values.push("MoveNext");
		if ((actions & AccessibilityAction.MovePrevious) != 0) values.push("MovePrevious");
		return values.length == 0 ? "(none)" : values.join(", ");
	}

	public static function rectText(rect:Rect):String
		return '${Std.int(rect.x)},${Std.int(rect.y)} ${Std.int(rect.width)}×${Std.int(rect.height)}';
}
