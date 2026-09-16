package pages;

import Insets;
import LayoutAxis;
import LayoutStyle;
import UiExplorer;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.ComboBox;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.RadioGroup;
import nativekit.ui.widgets.RadioOption;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Spinner;
import nativekit.ui.widgets.SpinnerKind;
import nativekit.ui.widgets.Select;
import nativekit.ui.widgets.SelectOption;
import nativekit.ui.widgets.Toggle;

/** Interactive primitive controls and their common input states. */
class ControlsPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Controls",
			"Focus, hover, pressed, selected and disabled states are part of each widget.");
		items.push(explorer.keyed("controls-row", new Row("controls-row", [
			explorer.keyed("buttons", explorer.panel("buttons-card", [
				explorer.keyed("heading", explorer.heading("Buttons")),
				explorer.keyed("primary", explorer.button("Primary action", "primary-action", function() {
					explorer.state.controls.progress = Math.min(1.0, explorer.state.controls.progress + 0.08);
				})),
				explorer.keyed("secondary", explorer.button("Selected", "selected-action", function() {}, true)),
				explorer.keyed("disabled", explorer.disabledButton("Disabled action")),
				explorer.keyed("hint", explorer.caption("Tab to focus · Enter to activate"))
			])),
			explorer.keyed("selection", explorer.panel("selection-card", [
				explorer.keyed("heading", explorer.heading("Selection")),
				explorer.keyed("checkbox", new Checkbox("show-labels", "Show labels", explorer.state.controls.checked,
					function(value) { explorer.state.controls.checked = value; })),
				explorer.keyed("toggle", new Toggle("control-enabled", "Live updates", explorer.state.controls.enabled,
					function(value) { explorer.state.controls.enabled = value; })),
				explorer.keyed("radio", new RadioGroup("density", [
					new RadioOption("comfortable", "Comfortable", "comfortable"),
					new RadioOption("compact", "Compact", "compact"),
					new RadioOption("disabled", "Unavailable", "disabled", false)
				], explorer.state.controls.radioValue, function(value) { explorer.state.controls.radioValue = value; })),
				explorer.keyed("select", new Select("density-select", [
					new SelectOption("comfortable", "Comfortable", "comfortable"),
					new SelectOption("compact", "Compact", "compact"),
					new SelectOption("disabled", "Unavailable", "disabled", false)
				], explorer.state.controls.selectValue, function(value) {
					explorer.state.controls.selectValue = value;
				})),
				explorer.keyed("combo", new ComboBox("density-combo", [
					new SelectOption("comfortable", "Comfortable", "comfortable"),
					new SelectOption("compact", "Compact", "compact"),
					new SelectOption("disabled", "Unavailable", "disabled", false)
				], explorer.state.controls.comboValue, function(value) {
					explorer.state.controls.comboValue = value;
				}))
			]))
		], explorer.rowStyle(14.0))));
		items.push(explorer.keyed("controls-range", explorer.panel("range-card", [
			explorer.keyed("heading", explorer.heading("Range and progress")),
			explorer.keyed("slider-label", explorer.label('Opacity / volume  ·  ${Std.int(explorer.state.controls.volume * 100)}%')),
			explorer.keyed("slider", explorer.slider()),
			explorer.keyed("progress-label", explorer.label('Progress  ·  ${Std.int(explorer.state.controls.progress * 100)}%')),
			explorer.keyed("progress", new ProgressBar("showcase-progress", explorer.state.controls.progress,
				0.0, 1.0, "Task progress")),
			explorer.keyed("advance", explorer.button("Advance progress", "advance-progress", function() {
				explorer.state.controls.progress = explorer.state.controls.progress >= 1.0 ? 0.0 : Math.min(1.0, explorer.state.controls.progress + 0.1);
				}))
			])));
		items.push(explorer.keyed("controls-spinners", explorer.panel("spinners-card", [
			explorer.keyed("heading", explorer.heading("Indeterminate spinners")),
			explorer.keyed("description", explorer.caption("Procedural indicators share retained paint resources and the frame scheduler.")),
			explorer.keyed("variants", new Row("spinner-variants", [
				explorer.keyed("ring", spinnerSample(explorer, "ring", "Ring", SpinnerKind.Ring)),
				explorer.keyed("dots", spinnerSample(explorer, "dots", "Dots", SpinnerKind.Dots)),
				explorer.keyed("bars", spinnerSample(explorer, "bars", "Bars", SpinnerKind.Bars)),
				explorer.keyed("pulse", spinnerSample(explorer, "pulse", "Pulse", SpinnerKind.Pulse))
		], explorer.rowStyle(10.0))),
			explorer.keyed("spinner-controls", new Row("spinner-controls", [
				explorer.keyed("status", explorer.caption(
					explorer.state.controls.spinnerRunning ? "Animating" : "Paused")),
				explorer.keyed("toggle", explorer.button(
					explorer.state.controls.spinnerRunning ? "Pause spinners" : "Resume spinners",
					"toggle-spinners", function() {
						explorer.state.controls.spinnerRunning = !explorer.state.controls.spinnerRunning;
					}))
		], explorer.rowStyle(10.0)))
		])));
	}

	static function spinnerSample(explorer:UiExplorer, key:String, label:String,
			kind:SpinnerKind):Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(82.0);
		style.padding = new Insets(8.0, 8.0, 8.0, 8.0);
		style.childGap = 6.0;
		style.background = explorer.state.lightTheme
			? UiExplorer.color(0.91, 0.94, 0.98) : UiExplorer.color(0.07, 0.10, 0.16);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		var spinner = new Spinner(key + "-spinner", label, null, kind, null, 1.0);
		spinner.running = explorer.state.controls.spinnerRunning;
		return new Column(key + "-sample", [
			explorer.keyed("indicator", new Align(key + "-align",
				spinner)),
			explorer.keyed("label", explorer.caption(label))
		], style);
	}
}
