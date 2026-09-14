package pages;

import UiExplorer;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.RadioGroup;
import nativekit.ui.widgets.RadioOption;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Toggle;

/** Interactive primitive controls and their common input states. */
class ControlsPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Controls",
			"Focus, hover, pressed, selected and disabled states are part of each widget.");
		items.push(explorer.keyed("controls-row", new Row("controls-row", [
			explorer.keyed("buttons", explorer.panel("buttons-card", [
				explorer.keyed("heading", explorer.text("Buttons", explorer.paletteText())),
				explorer.keyed("primary", explorer.button("Primary action", "primary-action", function() {
					explorer.progress = Math.min(1.0, explorer.progress + 0.08);
				})),
				explorer.keyed("secondary", explorer.button("Selected", "selected-action", function() {}, true)),
				explorer.keyed("disabled", explorer.disabledButton("Disabled action")),
				explorer.keyed("hint", explorer.text("Tab to focus · Enter to activate", explorer.paletteMuted()))
			])),
			explorer.keyed("selection", explorer.panel("selection-card", [
				explorer.keyed("heading", explorer.text("Selection", explorer.paletteText())),
				explorer.keyed("checkbox", new Checkbox("show-labels", "Show labels", explorer.checked,
					function(value) { explorer.checked = value; })),
				explorer.keyed("toggle", new Toggle("control-enabled", "Live updates", explorer.enabled,
					function(value) { explorer.enabled = value; })),
				explorer.keyed("radio", new RadioGroup("density", [
					new RadioOption("comfortable", "Comfortable", "comfortable"),
					new RadioOption("compact", "Compact", "compact"),
					new RadioOption("disabled", "Unavailable", "disabled", false)
				], explorer.radioValue, function(value) { explorer.radioValue = value; }))
			]))
		], explorer.rowStyle(14.0))));
		items.push(explorer.keyed("controls-range", explorer.panel("range-card", [
			explorer.keyed("heading", explorer.text("Range and progress", explorer.paletteText())),
			explorer.keyed("slider-label", explorer.text('Opacity / volume  ·  ${Std.int(explorer.volume * 100)}%',
				explorer.paletteMuted())),
			explorer.keyed("slider", explorer.slider()),
			explorer.keyed("progress-label", explorer.text('Progress  ·  ${Std.int(explorer.progress * 100)}%',
				explorer.paletteMuted())),
			explorer.keyed("progress", new ProgressBar("showcase-progress", explorer.progress,
				0.0, 1.0, "Task progress")),
			explorer.keyed("advance", explorer.button("Advance progress", "advance-progress", function() {
				explorer.progress = explorer.progress >= 1.0 ? 0.0 : Math.min(1.0, explorer.progress + 0.1);
			}))
		])));
	}
}
