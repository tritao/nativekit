package pages;

import Canvas;
import Color;
import Insets;
import LayoutAxis;
import LayoutStyle;
import Rect;
import UiExplorer;
import nativekit.ui.style.BackgroundDecoration;
import nativekit.ui.style.BorderDecoration;
import nativekit.ui.style.DecorationChain;
import nativekit.ui.style.GradientDecoration;
import nativekit.ui.style.ImageDecoration;
import nativekit.ui.style.NineSliceDecoration;
import nativekit.ui.style.ShadowDecoration;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleValue;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Showcase for first-class, stylesheet-driven post-layout decorations. */
class DecorationsPage {
	/** Registers the page examples as ordinary application style rules. */
	public static function installStyles(explorer:UiExplorer):Void {
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("decorations-card"), [
			StyleValue.decorations(DecorationChain.of([
				new ShadowDecoration(Color.rgba(0.04, 0.08, 0.16, 0.34), 0.0, 5.0, 10.0, 0.0),
				new BackgroundDecoration(Color.rgba(0.97, 0.98, 1.0, 1.0)),
				new BorderDecoration(Color.rgba(0.73, 0.80, 0.91, 1.0), 1.0)
			]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("decorations-gradient"), [
			StyleValue.decorations(DecorationChain.of([
				new GradientDecoration(Color.rgba(0.10, 0.42, 0.84, 1.0),
					Color.rgba(0.12, 0.72, 0.56, 1.0)),
				new BorderDecoration(Color.rgba(0.68, 0.88, 1.0, 0.82), 2.0)
			]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("decorations-image"), [
			StyleValue.decorations(DecorationChain.of([
				new ImageDecoration(explorer.demoImage),
				new BorderDecoration(Color.rgba(1.0, 1.0, 1.0, 0.72), 2.0)
			]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("decorations-nine-slice"), [
			StyleValue.decorations(DecorationChain.of([
				new ShadowDecoration(Color.rgba(0.03, 0.07, 0.14, 0.28), 0.0, 3.0, 6.0, 0.0),
				new NineSliceDecoration(explorer.demoNineSliceImage, 10.0, 10.0, 10.0, 10.0)
			]))
		]);
	}

	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Decorations",
			"Compose geometry and image decoration values through the same stylesheet cascade as every other visual property.");
		items.push(explorer.keyed("decorations-intro", explorer.panel("decorations-intro-panel", [
			explorer.keyed("heading", explorer.heading("Visual layers stay in the style system")),
			explorer.keyed("copy", explorer.caption(
			"Backgrounds, gradients, borders, images, nine-slice frames, and shadows are ordered values. They can be selected by keys, inspected, copied, and transitioned without creating ad-hoc paint callbacks.")),
			explorer.keyed("hint", explorer.caption(
			"Inspect a preview to see the resolved decoration chain. Resize the window to verify that geometry decorations follow the node bounds."))
		])));

		items.push(explorer.keyed("decorations-basic", explorer.panel("decorations-basic-panel", [
			explorer.keyed("heading", explorer.heading("Geometry and color")),
			explorer.keyed("copy", explorer.caption("Each preview uses a normal CanvasView. The stylesheet supplies its decoration chain before the small content painter runs.")),
			explorer.keyed("samples", new Row("decorations-basic-row", [
				explorer.keyed("card", sample(explorer, "decorations-card", "Background + border + shadow",
					"Three ordered decorations form a reusable surface.")),
				explorer.keyed("gradient", sample(explorer, "decorations-gradient", "GPU gradient + border",
					"The gradient is one native paint operation, not a band approximation."))
		], explorer.rowStyle(12.0)))
		])));

		items.push(explorer.keyed("decorations-assets", explorer.panel("decorations-assets-panel", [
			explorer.keyed("heading", explorer.heading("Images and scalable frames")),
			explorer.keyed("copy", explorer.caption("Image-backed decorations share the same value and cascade contract. Nine-slice preserves its corners while the center stretches with the resolved geometry.")),
			explorer.keyed("samples", new Row("decorations-assets-row", [
				explorer.keyed("image", sample(explorer, "decorations-image", "Image decoration",
					"A decoded image fills the resolved bounds.")),
				explorer.keyed("nine-slice", sample(explorer, "decorations-nine-slice", "Nine-slice decoration",
					"Edge insets remain stable as the preview resizes."))
		], explorer.rowStyle(12.0)))
		])));
	}

	static function sample(explorer:UiExplorer, key:String, title:String,
		caption:String):Column {
		var style = explorer.panelStyle();
		style.width = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		return new Column(key + "-sample", [
			explorer.keyed("title", explorer.label(title)),
			explorer.keyed("preview", preview(explorer, key, caption)),
			explorer.keyed("caption", explorer.caption(caption)),
			explorer.keyed("inspect", explorer.caption("Inspect: " + key))
		], style);
	}

	static function preview(explorer:UiExplorer, key:String, label:String):CanvasView {
		var style = new LayoutStyle();
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(126.0);
		style.clipToParent = false;
		return new CanvasView(key, function(canvas:Canvas, geometry) {
			var inset = 18.0;
			var width = Math.max(30.0, geometry.width - inset * 2.0);
			var height = Math.max(24.0, geometry.height - inset * 2.0);
			canvas.fillRoundedRect(new Rect(inset + width * 0.08, inset + height * 0.18,
				width * 0.84, height * 0.18), 4.0,
				Color.rgba(1.0, 0.88, 0.34, 0.92));
			canvas.fillRoundedRect(new Rect(inset + width * 0.28, inset + height * 0.48,
				width * 0.56, height * 0.24), 4.0,
				Color.rgba(0.08, 0.15, 0.28, 0.78));
			canvas.fillRoundedRect(new Rect(inset + width * 0.06, inset + height * 0.72,
				width * 0.26, height * 0.12), 3.0,
				Color.rgba(1.0, 1.0, 1.0, 0.84));
		}, style, label);
	}
}
