package pages;

import UiExplorer;
import components.DemoCard;
import components.DemoGrid;
import LayoutAxis;
import LayoutStyle;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Landing page for the Haxeon UI framework and its live catalog. */
class OverviewPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Haxeon UI",
			"A Haxe-native UI framework for stateful applications, accessible controls and rich graphics.");

		var metrics = explorer.context.frameMetrics;
		var nodeSummary = metrics == null
			? "Frame metrics become available after the first presentation."
			: '${metrics.nodeCount} retained nodes in the most recently completed frame.';
		items.push(explorer.keyed("overview-runtime", DemoGrid.build("overview-runtime-grid", [
			explorer.keyed("framework-card", DemoCard.build("framework-card", "FRAMEWORK",
				"Haxeon UI", "Widgets, state, styling, layout and semantics are owned in Haxe.",
				explorer.panelStyle(), explorer.context.buildContext.theme.accent)),
			explorer.keyed("host-card", DemoCard.build("host-card", "NATIVE DEPENDENCY",
				explorer.platformLabel, "NativeKit supplies the window, platform input and graphics surface.",
				explorer.panelStyle(), explorer.context.buildContext.theme.accent)),
			explorer.keyed("viewport-card", DemoCard.build("viewport-card", "LIVE VIEWPORT",
				'${Std.int(explorer.width)} × ${Std.int(explorer.height)}',
				nodeSummary,
				explorer.panelStyle(), explorer.context.buildContext.theme.accent))
		], explorer.rowStyle(10.0))));

		items.push(explorer.keyed("overview-explore", explorer.panel("overview-explore-panel", [
			explorer.keyed("explore-title", explorer.heading("Explore Haxeon UI")),
			explorer.keyed("explore-copy", explorer.caption(
				"Start with a capability, then use Inspect to examine its layout, style and accessibility semantics.")),
			explorer.keyed("explore-row-one", new Row("explore-row-one", [
				explorer.keyed("controls", destination(explorer, "Controls & state",
					"Buttons, selection, ranges and progress.", "controls")),
				explorer.keyed("divider-one", divider(explorer, "divider-one")),
				explorer.keyed("text", destination(explorer, "Text & input",
					"Editing, selection, IME and multilingual text.", "text")),
				explorer.keyed("divider-two", divider(explorer, "divider-two")),
				explorer.keyed("layout", destination(explorer, "Layout & data",
					"Responsive composition, scrolling and virtualization.", "lists"))
			], explorer.rowStyle(0.0))),
			explorer.keyed("explore-row-two", new Row("explore-row-two", [
				explorer.keyed("graphics", destination(explorer, "Graphics & 3D",
					"Paths, gradients, images and live 3D surfaces.", "graphics-paths")),
				explorer.keyed("divider-one", divider(explorer, "divider-one")),
				explorer.keyed("motion", destination(explorer, "Gestures & motion",
					"Pointer gestures, tweens and spring animation.", "gestures")),
				explorer.keyed("divider-two", divider(explorer, "divider-two")),
				explorer.keyed("overlays", destination(explorer, "Navigation & overlays",
					"Tabs, menus, popups, tooltips and dialogs.", "overlays"))
			], explorer.rowStyle(0.0)))
		])));

		items.push(explorer.keyed("overview-pipeline", explorer.panel("overview-pipeline-panel", [
			explorer.keyed("pipeline-title", explorer.heading("From Haxe to pixels")),
			explorer.keyed("pipeline-copy", explorer.caption(
				"Haxeon keeps application behavior and UI policy together while its NativeKit dependency handles platform integration and presentation.")),
			explorer.keyed("pipeline-stages", DemoGrid.build("pipeline-stages", [
				explorer.keyed("tree", stage(explorer, "1 · COMPOSE", "Widget tree",
					"Stable keys preserve state and focus.")),
				explorer.keyed("resolve", stage(explorer, "2 · RESOLVE", "Style & layout",
					"Theme rules become measured bounds.")),
				explorer.keyed("retain", stage(explorer, "3 · RETAIN", "Display list",
					"Unchanged work is reused across frames.")),
				explorer.keyed("present", stage(explorer, "4 · PRESENT", "NativeKit",
					"The native dependency composites and presents."))
		], explorer.rowStyle(10.0)))
		])));
	}

	static function destination(explorer:UiExplorer, title:String, description:String,
			page:String):Column {
		return new Column("destination-" + page, [
			explorer.keyed("title", explorer.label(title)),
			explorer.keyed("description", explorer.caption(description)),
			explorer.keyed("open", explorer.button("Open", "open-" + page, function() {
				explorer.state.selectedPage = page;
			}))
		], destinationStyle(explorer));
	}

	static function destinationStyle(explorer:UiExplorer):LayoutStyle {
		var style = explorer.panelStyle();
		style.width = LayoutAxis.percent(0.332);
		style.padding.left = 16.0;
		style.padding.right = 16.0;
		return style;
	}

	static function divider(explorer:UiExplorer, key:String):Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(1.0);
		style.height = LayoutAxis.grow();
		style.background = explorer.context.buildContext.theme.tokens.border;
		return new Column(key, [], style);
	}

	static function stage(explorer:UiExplorer, eyebrow:String, title:String,
			description:String):Column {
		return DemoCard.build("pipeline-" + title.toLowerCase().split(" ").join("-"), eyebrow,
			title, description, explorer.panelStyle(), explorer.context.buildContext.theme.accent);
	}
}
