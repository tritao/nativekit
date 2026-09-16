package pages;

import UiExplorer;
import Color;
import GradientStop;
import LayoutAxis;
import LayoutStyle;
import LineCap;
import LineJoin;
import PathBuilder;
import Rect;
import nativekit.ui.core.View;
import nativekit.ui.widgets.CanvasView;
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
			"See how path geometry becomes strokes, and how paint, opacity, and clipping change the result.");
		items.push(explorer.keyed("path-preview", demoPanel(explorer, "Vector paths become visible strokes",
			"The blue curve is a cubic Bézier. The green zig-zag joins line segments. The purple triangle is closed by connecting its final point back to its first.",
			pathPreview(explorer))));
		items.push(explorer.keyed("paint-preview", demoPanel(explorer, "Paint, opacity, and clipping",
			"Blue is painted first. A translucent green layer blends over it. The purple shape is wider than its outlined region, but clipping hides the overflow.",
			paintPreview(explorer))));
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

	public static function buildGradients(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Gradients",
			"Compare direction, multi-stop interpolation, and alpha over a visible background.");
		items.push(explorer.keyed("gradient-two-stop", demoPanel(explorer,
			"Two-stop horizontal gradient",
			"A linear gradient interpolates smoothly from the first stop at 0% to the second at 100%.",
			gradientPreview(explorer, "two-stop", 0))));
		items.push(explorer.keyed("gradient-multi-stop", demoPanel(explorer,
			"Direction and multiple stops",
			"Changing the gradient vector makes interpolation diagonal; a middle stop introduces a third color at 50%.",
			gradientPreview(explorer, "multi-stop", 1))));
		items.push(explorer.keyed("gradient-alpha", demoPanel(explorer,
			"Transparent color stops",
			"The checkerboard remains visible where the gradient fades, demonstrating interpolated alpha rather than a blend toward white.",
			gradientPreview(explorer, "alpha", 2))));
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

	static function pathPreview(explorer:UiExplorer):View {
		return new CanvasView("paths-canvas", function(canvas, geometry) {
			var width = geometry.width;
			canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height),
				explorer.state.lightTheme ? UiExplorer.color(0.93, 0.95, 0.98) : UiExplorer.color(0.07, 0.10, 0.16));
			canvas.strokeTransient(new PathBuilder().moveTo(18.0, 44.0)
				.cubicTo(width * 0.18, -4.0, width * 0.30, 88.0, width * 0.44, 38.0).build(),
				Color.rgba(0.18, 0.48, 0.82, 1.0), 4.0, LineCap.Round, LineJoin.Round);
			canvas.strokeTransient(new PathBuilder().moveTo(width * 0.50, 62.0)
				.lineTo(width * 0.59, 22.0).lineTo(width * 0.68, 62.0).build(),
				Color.rgba(0.16, 0.62, 0.44, 1.0), 6.0, LineCap.Round, LineJoin.Round);
			canvas.strokeTransient(new PathBuilder().moveTo(width * 0.78, 64.0)
				.lineTo(width * 0.87, 18.0).lineTo(width * 0.96, 64.0).close().build(),
				Color.rgba(0.50, 0.28, 0.72, 1.0), 4.0, LineCap.Round, LineJoin.Round);
		}, previewStyle(explorer),
			"Cubic curve, joined line segments, and a closed triangular path");
	}

	static function paintPreview(explorer:UiExplorer):View {
		return new CanvasView("paint-canvas", function(canvas, geometry) {
			canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height),
				explorer.state.lightTheme ? UiExplorer.color(0.93, 0.95, 0.98) : UiExplorer.color(0.07, 0.10, 0.16));
			var top = 18.0;
			var height = geometry.height - 36.0;
			canvas.fillRectIfPositive(new Rect(18.0, top, geometry.width * 0.38, height),
				Color.rgba(0.18, 0.48, 0.82, 1.0));
			canvas.withLayer(0.58, function(layer) {
				layer.fillRectIfPositive(new Rect(geometry.width * 0.24, top + 10.0,
					geometry.width * 0.34, height - 4.0), Color.rgba(0.16, 0.70, 0.46, 1.0));
			});
			var clip = new Rect(geometry.width * 0.68, top, geometry.width * 0.24, height);
			var border = explorer.context.buildContext.theme.mutedText;
			canvas.fillRectIfPositive(new Rect(clip.x - 1.0, clip.y - 1.0,
				clip.width + 2.0, clip.height + 2.0), border);
			canvas.withClip(clip, function(clipped) {
				clipped.fillRectIfPositive(new Rect(clip.x - 24.0, clip.y + 10.0,
					clip.width + 48.0, clip.height - 20.0), Color.rgba(0.50, 0.28, 0.72, 1.0));
			});
		}, previewStyle(explorer),
			"Opaque paint, translucent overlap, and a shape clipped to bounds");
	}

	static function previewStyle(explorer:UiExplorer):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(92.0);
		style.background = explorer.state.lightTheme
			? UiExplorer.color(0.93, 0.95, 0.98) : UiExplorer.color(0.07, 0.10, 0.16);
		style.radiusTopLeft = style.radiusTopRight = 6.0;
		style.radiusBottomLeft = style.radiusBottomRight = 6.0;
		style.clipToParent = true;
		return style;
	}

	static function gradientPreview(explorer:UiExplorer, key:String, kind:Int):View {
		return new CanvasView("gradient-" + key, function(canvas, geometry) {
			var bounds = new Rect(14.0, 14.0, geometry.width - 28.0, geometry.height - 28.0);
			if (kind == 2) {
				var size = 12.0;
				var columns = Std.int(Math.ceil(bounds.width / size));
				var rows = Std.int(Math.ceil(bounds.height / size));
				for (row in 0...rows)
					for (column in 0...columns)
						canvas.fillRectIfPositive(new Rect(bounds.x + column * size,
							bounds.y + row * size, Math.min(size, bounds.width - column * size),
							Math.min(size, bounds.height - row * size)),
							(row + column) % 2 == 0 ? UiExplorer.color(0.82, 0.85, 0.90) :
							UiExplorer.color(0.96, 0.97, 0.99));
				canvas.fillLinearGradientRect(bounds, bounds.x, bounds.y,
					bounds.x + bounds.width, bounds.y, [
					new GradientStop(0.0, Color.rgba(0.18, 0.48, 0.82, 1.0)),
					new GradientStop(1.0, Color.rgba(0.18, 0.48, 0.82, 0.0))
				]);
			} else if (kind == 1) {
				canvas.fillLinearGradientRect(bounds, bounds.x, bounds.y,
					bounds.x + bounds.width, bounds.y + bounds.height, [
					new GradientStop(0.0, Color.rgba(0.16, 0.66, 0.48, 1.0)),
					new GradientStop(0.5, Color.rgba(0.50, 0.30, 0.76, 1.0)),
					new GradientStop(1.0, Color.rgba(0.94, 0.40, 0.55, 1.0))
				]);
			} else
				canvas.fillLinearGradientRect(bounds, bounds.x, bounds.y,
					bounds.x + bounds.width, bounds.y, [
					new GradientStop(0.0, Color.rgba(0.18, 0.48, 0.82, 1.0)),
					new GradientStop(1.0, Color.rgba(0.16, 0.70, 0.46, 1.0))
				]);
		}, previewStyle(explorer), key + " linear gradient preview");
	}
}
