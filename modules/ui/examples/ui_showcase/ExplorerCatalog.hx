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
			["button", "checkbox", "toggle", "radio", "select", "slider", "progress", "spinner", "loading", "indeterminate"],
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
			function(explorer, items) { OverlaysPage.build(explorer, items); }, "Overlays"),
		new ExplorerPage("gestures", "Gestures & Motion", "components", "Gesture arbitration and animation",
			["tap", "double tap", "long press", "drag", "tween", "spring", "animation"],
			function(explorer, items) { GesturesPage.build(explorer, items); }),
		new ExplorerPage("graphics-paths", "Paths & Paint", "graphics", "Vector geometry, strokes and compositing",
			["bezier", "path", "stroke", "join", "paint", "alpha", "blend"],
			function(explorer, items) { GraphicsPage.buildPaths(explorer, items); }),
		new ExplorerPage("graphics-gradients", "Gradients", "graphics", "Linear gradients, color stops and transparency",
			["gradient", "linear", "color stops", "alpha", "interpolation"],
			function(explorer, items) { GraphicsPage.buildGradients(explorer, items); }),
		new ExplorerPage("graphics-text", "Text Shaping", "graphics", "Multilingual shaping and text geometry",
			["font", "glyph", "bidi", "caret", "selection", "arabic", "hebrew", "japanese", "emoji"],
			function(explorer, items) { GraphicsPage.buildText(explorer, items); }),
		new ExplorerPage("graphics-images", "Images & Layers", "graphics", "Clipping, opacity and layered composition",
			["image", "clip", "layer", "opacity", "nine slice", "composite"],
			function(explorer, items) { GraphicsPage.buildImages(explorer, items); }),
		new ExplorerPage("graphics-rendering", "Rendering", "graphics", "Retained rendering and frame performance diagnostics",
			["display list", "retained", "renderer", "offscreen", "surface", "cube", "performance"],
			function(explorer, items) { GraphicsPage.buildRendering(explorer, items); })
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
			case "graphics": "GRAPHICS";
			default: group;
		};
	}
}
