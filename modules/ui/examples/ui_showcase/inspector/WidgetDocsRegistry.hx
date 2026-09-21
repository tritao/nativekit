package inspector;

import Rect;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;

/** API synopsis and canonical usage examples keyed by semantic widget role. */
class WidgetDocsRegistry {
	public static function describe(role:Int):{description:String, behavior:String, code:String} {
		return switch role {
			case AccessibilityRole.Button: {
				description: "A compositional action control. Haxe owns its interaction state and semantic action; the render tree contains a box and label.",
				behavior: "Activate with click, Enter or Space. It participates in keyboard focus and exposes an Activate accessibility action.",
				code: 'new Button("Save changes", style, onActivate, "save")'
			};
			case AccessibilityRole.Checkbox: {
				description: "A boolean selection control composed from ordinary Haxe layout and paint nodes.",
				behavior: "Click or press Space to toggle. Checked state is reflected in the semantic value and state bits.",
				code: 'new Checkbox("show-labels", "Show labels", checked, onChange)'
			};
			case AccessibilityRole.Radio: {
				description: "One option in an exclusive radio selection group.",
				behavior: "Arrow keys move between enabled options; Space or Enter selects the focused option.",
				code: 'new RadioGroup("density", options, selected, onChange)'
			};
			case AccessibilityRole.TextField: {
				description: "An editable text control backed by Haxeon editor state and native text-input services.",
				behavior: "Pointer hit testing positions the caret; keyboard selection/editing and platform clipboard and IME are supported where available.",
				code: 'new TextField("name", value, onChange, style, "Display name")'
			};
			case AccessibilityRole.Slider: {
				description: "A continuous range control with pointer, keyboard and semantic value support.",
				behavior: "Drag the thumb or track; arrow keys and accessibility increment/decrement adjust the snapped value.",
				code: 'new Slider("volume", "Volume", value, 0, 1, 0.01, onChange)'
			};
			case AccessibilityRole.ScrollArea: {
				description: "A clipped viewport that translates persistent content using Haxe-owned scroll state.",
				behavior: "Wheel, touch/drag, and semantic forward/back actions update the scroll controller.",
				code: 'new ScrollView("results", content, style, ScrollAxis.Vertical)'
			};
			case AccessibilityRole.List, AccessibilityRole.ListItem: {
				description: "A list container or a realized item within a data-oriented view.",
				behavior: "The model-backed list builds only visible rows plus a small overscan window, preserves stable item keys, and exposes collection semantics.",
				code: 'new ListView("rows", model, style, controller, viewportHeight)'
			};
			case AccessibilityRole.Image: {
				description: "A rendered image with an optional accessible name.",
				behavior: "The image participates in normal layout, clipping and render order.",
				code: 'new ImageView("avatar", image, "Profile photo")'
			};
			case AccessibilityRole.Heading: {
				description: "A heading that identifies a section of the current demo.",
				behavior: "Headings expose a navigable semantic role while rendering with the shared text pipeline.",
				code: 'new Text("Section title", headingStyle)'
			};
			case AccessibilityRole.Group: {
				description: "A layout or grouping node in the Haxe-owned render tree.",
				behavior: "It composes children and contributes resolved bounds, clipping and z-order without adding a native widget abstraction.",
				code: 'new Column("settings", [keyed("name", nameField)])'
			};
			case AccessibilityRole.Switch: {
				description: "A two-position control with switch semantics and Haxe-owned checked state.",
				behavior: "Click or press Space to toggle. Checked state is announced as a switch value.",
				code: 'new Toggle("notifications", "Notifications", enabled, onChange)'
			};
			case AccessibilityRole.ComboBox: {
				description: "A typed selection control with either a compact trigger or an editable query field and a keyboard-navigable option list.",
				behavior: "Click or press Enter/Space to open. ComboBox text filters options; arrow keys move between enabled options; long lists scroll, Enter selects, Escape or an outside click closes, and the list stays anchored to the control.",
				code: 'new ComboBox("density", options, selected, onChange)'
			};
			case AccessibilityRole.ProgressBar: {
				description: "A non-interactive determinate or indeterminate indicator driven by the shared animation scheduler.",
				behavior: "Determinate values animate visually while accessibility receives the current value immediately. Indeterminate mode reports busy state and respects reduced motion.",
				code: 'var progress = new ProgressBar("upload", value, 0, 1, "Upload progress");\nprogress.mode = ProgressMode.Indeterminate;'
			};
			case AccessibilityRole.Dialog: {
				description: "A modal surface for a focused task, composed above the application tree.",
				behavior: "Dismiss or complete the task with its controls; modal semantics and focus are exposed with the dialog.",
				code: 'new Dialog("confirm", "Confirm", content, onDismiss)'
			};
			case AccessibilityRole.Menu, AccessibilityRole.MenuBar, AccessibilityRole.MenuItem: {
				description: "A command surface or one of its actionable menu entries.",
				behavior: "Menu commands expose their labels, enabled state and activation action.",
				code: 'new Menu("file-menu", items, width, height, onDismiss)'
			};
			case AccessibilityRole.TabList, AccessibilityRole.Tab, AccessibilityRole.TabPanel: {
				description: "A tab navigation surface with an active tab and associated panel.",
				behavior: "Use arrow keys to move between tabs and Enter or Space to activate the focused tab.",
				code: 'new Tabs("settings", tabs, selected, onChange)'
			};
			default: {
				description: "A render primitive produced by a Haxe widget or layout composition.",
				behavior: "Geometry and clipping are resolved natively; events and semantic identity remain connected to this Haxe node.",
				code: 'new Text("Hello, Haxeon UI")'
			};
		};
	}

	public static function roleName(role:Int):String {
		return switch role {
			case AccessibilityRole.Group: "Group";
			case AccessibilityRole.Button: "Button";
			case AccessibilityRole.Checkbox: "Checkbox";
			case AccessibilityRole.Radio: "Radio";
			case AccessibilityRole.Text: "Text";
			case AccessibilityRole.TextField: "TextField";
			case AccessibilityRole.Link: "Link";
			case AccessibilityRole.Image: "Image";
			case AccessibilityRole.Heading: "Heading";
			case AccessibilityRole.List: "List";
			case AccessibilityRole.ListItem: "ListItem";
			case AccessibilityRole.Slider: "Slider";
			case AccessibilityRole.ScrollArea: "ScrollArea";
			case AccessibilityRole.Dialog: "Dialog";
			case AccessibilityRole.Menu: "Menu";
			case AccessibilityRole.MenuBar: "MenuBar";
			case AccessibilityRole.MenuItem: "MenuItem";
			case AccessibilityRole.TabList: "TabList";
			case AccessibilityRole.Tab: "Tab";
			case AccessibilityRole.TabPanel: "TabPanel";
			case AccessibilityRole.Switch: "Switch";
			case AccessibilityRole.ProgressBar: "ProgressBar";
			case AccessibilityRole.ComboBox: "ComboBox";
			case AccessibilityRole.Collection: "Collection";
			case AccessibilityRole.CollectionItem: "CollectionItem";
			case AccessibilityRole.Grid: "Grid";
			case AccessibilityRole.Row: "Row";
			case AccessibilityRole.Cell: "Cell";
			case AccessibilityRole.ColumnHeader: "ColumnHeader";
			case AccessibilityRole.RowHeader: "RowHeader";
			case AccessibilityRole.Tree: "Tree";
			case AccessibilityRole.TreeItem: "TreeItem";
			case AccessibilityRole.Separator: "Separator";
			case AccessibilityRole.Toolbar: "Toolbar";
			case AccessibilityRole.Status: "Status";
			case AccessibilityRole.Alert: "Alert";
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
