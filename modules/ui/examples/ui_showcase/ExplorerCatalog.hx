import pages.ControlsPage;
import pages.GesturesPage;
import pages.GraphicsPage;
import pages.LayoutPage;
import pages.ListsPage;
import pages.OverlaysPage;
import pages.OverviewPage;
import pages.TextPage;

/** Canonical inventory for the NativeKit UI Explorer's pages and navigation. */
class ExplorerCatalog {
	static final pageList:Array<ExplorerPage> = [
		new ExplorerPage("overview", "Overview", "start", "Framework and runtime overview",
			["welcome", "native", "wasm", "runtime"],
			function(explorer, items) { OverviewPage.build(explorer, items); }),
		new ExplorerPage("controls", "Controls", "components", "Interactive control states",
			["button", "checkbox", "toggle", "radio", "slider", "progress"],
			function(explorer, items) { ControlsPage.build(explorer, items); }),
		new ExplorerPage("text", "Text & Input", "components", "Text editing and platform input",
			["textfield", "textarea", "ime", "selection", "clipboard", "multilingual"],
			function(explorer, items) { TextPage.build(explorer, items); }),
		new ExplorerPage("layout", "Layout", "components", "Composable layout primitives",
			["row", "column", "padding", "align", "stack", "clip", "z-order"],
			function(explorer, items) { LayoutPage.build(explorer, items); }),
		new ExplorerPage("lists", "Scrolling & Data", "components", "Scrolling and fixed-row virtualization",
			["scrollview", "virtuallist", "virtualization", "10000 rows"],
			function(explorer, items) { ListsPage.build(explorer, items); }),
		new ExplorerPage("overlays", "Navigation & Overlays", "components", "Tabs and floating UI",
			["tabs", "popup", "menu", "tooltip", "dialog", "overlay"],
			function(explorer, items) { OverlaysPage.build(explorer, items); }),
		new ExplorerPage("gestures", "Gestures & Motion", "components", "Gesture arbitration and animation",
			["tap", "double tap", "long press", "drag", "tween", "spring", "animation"],
			function(explorer, items) { GesturesPage.build(explorer, items); }),
		new ExplorerPage("graphics", "Graphics Lab", "developer", "Retained graphics demonstrations",
			["paths", "text shaping", "images", "cube", "renderer"],
			function(explorer, items) { GraphicsPage.build(explorer, items); })
	];

	public static function all():Array<ExplorerPage>
		return pageList;

	public static function find(id:String):Null<ExplorerPage> {
		for (page in pageList)
			if (page.id == id)
				return page;
		return null;
	}

	public static function matches(page:ExplorerPage, needle:String):Bool {
		if (UiExplorer.containsInsensitive(page.title, needle) ||
				UiExplorer.containsInsensitive(page.description, needle))
			return true;
		for (keyword in page.keywords)
			if (UiExplorer.containsInsensitive(keyword, needle))
				return true;
		return false;
	}

	public static function groupTitle(group:String):String {
		return switch group {
			case "start": "START HERE";
			case "components": "COMPONENTS";
			case "developer": "DEVELOPER TOOLS";
			default: group;
		};
	}
}
