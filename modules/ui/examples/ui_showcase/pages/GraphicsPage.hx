package pages;

import UiExplorer;
import Color;
import nativekit.ui.core.View;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** The original retained graphics workload, presented as an Explorer page. */
class GraphicsPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Graphics Lab",
			"Focused, inspectable graphics examples backed by the same retained renderer.");
		items.push(explorer.keyed("graphics-card", explorer.panel("graphics-card", [
			explorer.keyed("heading", explorer.heading("Explore one graphics system at a time")),
			explorer.keyed("copy", explorer.caption("Use the Graphics Lab catalog entries for paths and paints, multilingual text shaping, image layers, and retained rendering. Every focused preview participates in layout, inspection, accessibility reporting, and headless visual coverage.")),
			explorer.keyed("launch", explorer.button("Open full Graphics Lab", "open-graphics-lab",
				explorer.onOpenGraphics)),
			explorer.keyed("hint", explorer.caption("The full lab remains the combined renderer stress scene. Press Escape to return."))
		])));
		items.push(explorer.keyed("graphics-pipeline", explorer.panel("graphics-pipeline", [
			explorer.keyed("heading", explorer.heading("One compositor, two authoring levels")),
			explorer.keyed("copy", explorer.caption("Framework-native previews compose CanvasView with ordinary UI. The full lab exercises the lower-level typed graphics API directly. Both produce retained display lists consumed by NativeKit's renderer."))
		])));
	}

	public static function buildPaths(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Paths & Paint",
			"Inspect vector paths, stroke caps and joins, alpha layers, and clipping.");
		items.push(explorer.keyed("path-preview", demoPanel(explorer, "Vector geometry",
			"A cubic Bézier, a closed polygon, and round stroke geometry are encoded into a retained display list.",
			preview(explorer, "path", ["Cubic Bézier", "Round joins", "Closed path"]))));
		items.push(explorer.keyed("paint-preview", demoPanel(explorer, "Paint and opacity",
			"Overlapping translucent layers make compositing order visible without leaving the UI tree.",
			preview(explorer, "paint", ["Solid paint", "68% layer", "Source over"]))));
	}

	public static function buildText(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Text Shaping",
			"Exercise script direction, fallback fonts, emoji, and caret-aware text geometry.");
		items.push(explorer.keyed("script-samples", explorer.panel("script-samples", [
			explorer.keyed("heading", explorer.heading("One paragraph engine, multiple scripts")),
			explorer.keyed("latin", explorer.text("NativeKit shapes text across platform and WebAssembly runtimes.")),
			explorer.keyed("arabic", explorer.text("مرحبا بالعالم — تخطيط من اليمين إلى اليسار")),
			explorer.keyed("hebrew", explorer.text("שלום עולם — טקסט דו־כיווני")),
			explorer.keyed("japanese", explorer.text("こんにちは世界 — NativeKit UI")),
			explorer.keyed("emoji", explorer.text("Fallback clusters stay together: 👋 🌍 ✨")),
			explorer.keyed("hint", explorer.caption("Try this: inspect each text node, then compare its resolved geometry and semantic label."))
		])));
		items.push(explorer.keyed("text-geometry", explorer.panel("text-geometry", [
			explorer.keyed("heading", explorer.heading("Caret and selection geometry")),
			explorer.keyed("copy", explorer.caption("The Text & Input page exposes interactive caret movement, selection, clipboard, and IME composition. This entry isolates the shaping inputs used by those controls."))
		])));
	}

	public static function buildImages(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Images & Layers",
			"Understand clipping and layer composition before introducing external image assets.");
		items.push(explorer.keyed("clip-preview", demoPanel(explorer, "Clipped composition",
			"The preview deliberately paints beyond its rounded visual region; the layout node owns clipping.",
			preview(explorer, "image", ["Image source", "Clip bounds", "Composited layer"]))));
		items.push(explorer.keyed("image-roadmap", explorer.panel("image-roadmap", [
			explorer.keyed("heading", explorer.heading("Image resources and nine-slice")),
			explorer.keyed("copy", explorer.caption("The renderer supports sampled images, clipped image layers, and nine-slice decoration. The next focused sample should expose asset loading, fit modes, sampling, and loading/error states as one reusable Image component."))
		])));
	}

	public static function buildRendering(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Rendering & Performance",
			"See what NativeKit retains, reuses, and submits each frame.");
		items.push(explorer.keyed("pipeline", explorer.panel("pipeline", [
			explorer.keyed("heading", explorer.heading("Submit → resolve → retain → render")),
			explorer.keyed("copy", explorer.caption("Stable widget IDs preserve state. Layout resolves geometry. Custom paint compiles into display lists. Unchanged submissions and paint caches can be reused across frames.")),
			explorer.keyed("hint", explorer.caption("Open Inspect and select the preview to compare Haxe identity, resolved geometry, and paint participation."))
		])));
		items.push(explorer.keyed("frame-preview", demoPanel(explorer, "Frame workload",
			"A compact visualization of retained work: layout, paint compilation, and compositor submission.",
			preview(explorer, "frame", ["Layout 72%", "Paint 46%", "Submit 88%"]))));
	}

	static function demoPanel(explorer:UiExplorer, title:String, copy:String,
			preview:View):View {
		return explorer.panel("demo", [
			explorer.keyed("heading", explorer.heading(title)),
			explorer.keyed("copy", explorer.caption(copy)),
			explorer.keyed("preview", preview),
			explorer.keyed("try", explorer.caption("Try this: inspect the preview, resize the catalog pane, and switch themes."))
		]);
	}

	static function preview(explorer:UiExplorer, key:String, labels:Array<String>):View {
		var colors:Array<Color> = [Color.rgba(0.18, 0.48, 0.82, 1.0),
			Color.rgba(0.20, 0.66, 0.50, 1.0), Color.rgba(0.52, 0.30, 0.74, 1.0)];
		var children:Array<KeyedView> = [];
		for (index in 0...labels.length)
			children.push(explorer.keyed(key + "-" + index,
				explorer.colorTile(labels[index], colors[index % colors.length])));
		return new Row(key + "-preview", children, explorer.rowStyle(8.0));
	}
}
