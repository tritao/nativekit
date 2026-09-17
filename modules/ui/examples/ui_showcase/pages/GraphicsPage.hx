package pages;

import UiExplorer;
import Color;
import GradientStop;
import Image;
import LayoutAxis;
import LayoutStyle;
import LineCap;
import LineJoin;
import PathBuilder;
import Rect;
import nativekit.ui.core.View;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ImageFit;
import nativekit.ui.widgets.ImageView;
import nativekit.ui.widgets.LayeredImageView;
import nativekit.ui.widgets.LayeredImageView.ImageLayer;
import nativekit.ui.widgets.NineSliceView;
import nativekit.ui.widgets.Row;
import components.CubeView;

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
			explorer.keyed("copy", explorer.caption("Haxeon previews compose CanvasView with ordinary UI. The full lab exercises the lower-level typed graphics API directly. Both produce retained display lists consumed by the NativeKit rendering dependency."))
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
			explorer.keyed("latin", explorer.text("Haxeon shapes text consistently across supported runtimes.")),
			explorer.keyed("arabic", explorer.text("مرحبا بالعالم — تخطيط من اليمين إلى اليسار")),
			explorer.keyed("hebrew", explorer.text("שלום עולם — טקסט דו־כיווני")),
			explorer.keyed("japanese", explorer.text("こんにちは世界 — Haxeon UI")),
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

	public static function build3d(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "3D Views",
			"Native indexed meshes render with perspective and depth, then composite into ordinary UI layout.");
		items.push(explorer.keyed("cube-view", explorer.panel("cube-view", [
			explorer.keyed("heading", explorer.heading("Live perspective viewport")),
			explorer.keyed("copy", explorer.caption("A retained native surface continuously rotates an indexed cube with depth-tested face occlusion.")),
			explorer.keyed("preview", new CubeView("live-cube", 0.38,
				"Live rotating perspective cube", cubeStyle(explorer), true, 0.10)),
			explorer.keyed("try", explorer.caption("Try this: inspect the viewport and resize the split pane while it rotates."))
		])));
		items.push(explorer.keyed("3d-pipeline", explorer.panel("3d-pipeline", [
			explorer.keyed("heading", explorer.heading("Mesh to depth pass to sampled surface to compositor")),
			explorer.keyed("copy", explorer.caption("The producer submits 24 colored vertices and 36 indices with a model-view-projection matrix. The NativeKit rendering dependency draws offscreen with depth testing and Haxeon samples the result in its display list."))
		])));
	}

	static function cubeStyle(explorer:UiExplorer):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(Math.max(340.0, Math.min(520.0, explorer.height * 0.42)));
		style.background = Color.rgba(0.035, 0.055, 0.09, 1.0);
		style.radiusTopLeft = style.radiusTopRight = 6.0;
		style.radiusBottomLeft = style.radiusBottomRight = 6.0;
		style.clipToParent = true;
		return style;
	}


	public static function buildImages(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Images & Layers",
			"Compare intrinsic aspect fitting, clipped composition, opacity, and scalable image frames.");
		items.push(explorer.keyed("image-fit", explorer.panel("image-fit", [
			explorer.keyed("heading", explorer.heading("Fit modes")),
			explorer.keyed("copy", explorer.caption("Contain preserves the whole image, Cover fills and clips, and Stretch maps directly to the bounds.")),
			explorer.keyed("previews", new Row("image-fit-row", [
				explorer.keyed("contain", fitSample(explorer, "contain", ImageFit.Contain)),
				explorer.keyed("cover", fitSample(explorer, "cover", ImageFit.Cover)),
				explorer.keyed("stretch", fitSample(explorer, "stretch", ImageFit.Stretch))
			], imageRowStyle(explorer)))
		])));
		items.push(explorer.keyed("image-layers", explorer.panel("image-layers", [
			explorer.keyed("heading", explorer.heading("Layers and opacity")),
			explorer.keyed("copy", explorer.caption("Independent sampled images retain their own bounds and opacity, then composite in source order.")),
			explorer.keyed("preview", new LayeredImageView("layered-preview", [
				new ImageLayer(explorer.demoImage, 0.0, 0.0, 1.0, 1.0),
				new ImageLayer(explorer.demoOverlayImage, 0.76, 0.14, 0.14, 0.72, 0.72)
			], "Landscape image with a translucent violet circular layer", wideImageStyle(explorer)))
		])));
		items.push(explorer.keyed("image-assets", explorer.panel("image-assets", [
			explorer.keyed("heading", explorer.heading("Decoded assets and sampling")),
			explorer.keyed("copy", explorer.caption("The left preview is decoded from a PNG file. Magnified textures compare linear interpolation with nearest-texel sampling.")),
			explorer.keyed("previews", new Row("image-assets-row", [
				explorer.keyed("decoded", imageSample(explorer, "Decoded PNG", explorer.demoLoadedImage,
					ImageFit.Contain)),
				explorer.keyed("linear", imageSample(explorer, "Linear", explorer.demoPixelLinear,
					ImageFit.Stretch)),
				explorer.keyed("nearest", imageSample(explorer, "Nearest", explorer.demoPixelNearest,
					ImageFit.Stretch))
			], imageRowStyle(explorer)))
		])));
		items.push(explorer.keyed("nine-slice", explorer.panel("nine-slice", [
			explorer.keyed("heading", explorer.heading("Nine-slice scaling")),
			explorer.keyed("copy", explorer.caption("Corners retain their source size while edges and the center stretch to fill a wide destination.")),
			explorer.keyed("preview", new NineSliceView("nine-slice-preview", explorer.demoNineSliceImage,
				10.0, 10.0, 10.0, 10.0, "Blue nine-slice frame", wideImageStyle(explorer)))
		])));
	}

	static function fitSample(explorer:UiExplorer, key:String, fit:ImageFit):Column {
		return imageSample(explorer, key.substr(0, 1).toUpperCase() + key.substr(1),
			explorer.demoImage, fit);
	}

	static function imageSample(explorer:UiExplorer, label:String, source:Image,
			fit:ImageFit):Column {
		var key = label.toLowerCase().split(" ").join("-");
		var image = new ImageView(key + "-image", source,
			label + " image preview", imageSampleStyle(explorer));
		image.fit = fit;
		var style = new LayoutStyle();
		style.width = LayoutAxis.percent(0.32);
		style.childGap = 6.0;
		return new Column(key + "-sample", [
			explorer.keyed("label", explorer.caption(label)),
			explorer.keyed("image", image)
		], style);
	}

	static function imageRowStyle(explorer:UiExplorer):LayoutStyle {
		var style = explorer.rowStyle(10.0);
		style.width = LayoutAxis.stretch();
		return style;
	}

	static function imageSampleStyle(explorer:UiExplorer):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(150.0);
		style.background = explorer.state.lightTheme
			? UiExplorer.color(0.88, 0.91, 0.96) : UiExplorer.color(0.04, 0.06, 0.10);
		style.clipToParent = true;
		return style;
	}

	static function wideImageStyle(explorer:UiExplorer):LayoutStyle {
		var style = imageSampleStyle(explorer);
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(180.0);
		return style;
	}

	public static function buildRendering(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Rendering & Performance",
			"See what Haxeon retains, reuses, and submits each frame.");
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
