package pages;

import Canvas;
import Color;
import GradientStop;
import Insets;
import LayoutAxis;
import LayoutStyle;
import Rect;
import UiExplorer;
import nativekit.ui.style.BlurEffect;
import nativekit.ui.style.BrightnessEffect;
import nativekit.ui.style.ContrastEffect;
import nativekit.ui.style.DropShadowEffect;
import nativekit.ui.style.EffectChain;
import nativekit.ui.style.HueRotateEffect;
import nativekit.ui.style.Mask;
import nativekit.ui.style.SaturateEffect;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleValue;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Showcase for post-layout effects and their stylesheet-facing contracts. */
class EffectsPage {
	/** Installs the examples as ordinary application stylesheet declarations. */
	public static function installStyles(explorer:UiExplorer):Void {
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("effects-blur"), [
			StyleValue.effects(EffectChain.of([BlurEffect.withSigma(5.0)]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("effects-color"), [
			StyleValue.effects(EffectChain.of([
				new BrightnessEffect(1.12), new ContrastEffect(1.18),
				new SaturateEffect(1.35), HueRotateEffect.withDegrees(10.0)
			]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("effects-shadow"), [
			StyleValue.effects(EffectChain.of([
				new DropShadowEffect(0.0, 8.0, 7.0, Color.rgba(0.02, 0.06, 0.14, 0.55))
			]))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("effects-mask"), [
			StyleValue.effects(EffectChain.of([new DropShadowEffect(0.0, 4.0, 4.0,
				Color.rgba(0.05, 0.10, 0.20, 0.35))])),
			StyleValue.mask(Mask.linearGradient(0.0, 0.0, 1.0, 1.0, 0.28, 1.0))
		]);
		explorer.context.buildContext.styleSheet.rule(StyleSelector.key("effects-backdrop"), [
			StyleValue.backdropEffects(EffectChain.of([BlurEffect.withSigma(7.0)]))
		]);
	}

	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Effects",
			"Compose blur, color, shadow, mask, and backdrop passes through the Haxe stylesheet.");
		items.push(explorer.keyed("effects-intro", explorer.panel("effects-intro-panel", [
			explorer.keyed("heading", explorer.heading("Effects are part of the visual contract")),
			explorer.keyed("copy", explorer.caption(
			"Each preview is a normal CanvasView. The application stylesheet resolves its effect chain, the compositor allocates only the required intermediate targets, and Inspect exposes the chain, ink overflow, and estimated pass count.")),
			explorer.keyed("hint", explorer.caption(
			"Resize the window or inspect a preview to see the effect bounds follow the resolved geometry."))
		])));

		items.push(explorer.keyed("effects-basic", explorer.panel("effects-basic-panel", [
			explorer.keyed("heading", explorer.heading("Foreground effects")),
			explorer.keyed("copy", explorer.caption("These passes process the preview's rendered pixels in order.")),
			explorer.keyed("samples", new Row("effects-basic-row", [
				explorer.keyed("blur", sample(explorer, "effects-blur", "Blur", "blur(5)",
					"Softens the entire rendered source.")),
				explorer.keyed("color", sample(explorer, "effects-color", "Color matrix", "brightness · contrast · saturate · hue",
					"Color adjustments collapse into one compositor matrix.")),
				explorer.keyed("shadow", sample(explorer, "effects-shadow", "Drop shadow", "drop-shadow(0, 8, 7)",
					"Preserves the source while expanding its ink overflow."))
		], explorer.rowStyle(12.0)))
		])));

		items.push(explorer.keyed("effects-composition", explorer.panel("effects-composition-panel", [
			explorer.keyed("heading", explorer.heading("Masks and composition input")),
			explorer.keyed("copy", explorer.caption("Masks constrain source alpha. Backdrop effects remain registered in the style model while their live preview is temporarily disabled.")),
			explorer.keyed("samples", new Row("effects-composition-row", [
				explorer.keyed("mask", sample(explorer, "effects-mask", "Gradient mask", "mask(linear-gradient)",
					"The source fades toward one edge while retaining its shadow.")),
			explorer.keyed("backdrop-disabled", disabledSample(explorer))
		], explorer.rowStyle(12.0)))
		])));
	}

	static function disabledSample(explorer:UiExplorer):Column {
		var style = explorer.panelStyle();
		style.width = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		return new Column("effects-backdrop-disabled-card", [
			explorer.keyed("title", explorer.label("Backdrop blur")),
			explorer.keyed("state", explorer.caption("Temporarily disabled in the live preview")),
			explorer.keyed("description", explorer.caption(
				"The Haxe style rule remains registered for inspection while the native custom-paint pass dependency is finalized."))
		], style);
	}

	static function sample(explorer:UiExplorer, key:String, title:String,
		description:String, caption:String):Column {
		var style = explorer.panelStyle();
		style.width = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		return new Column(key + "-card", [
			explorer.keyed("title", explorer.label(title)),
			explorer.keyed("preview", preview(explorer, key, caption)),
			explorer.keyed("description", explorer.caption(description)),
			explorer.keyed("inspect", explorer.caption("Inspect: " + key))
		], style);
	}

	static function preview(explorer:UiExplorer, key:String, caption:String):CanvasView {
		var style = new LayoutStyle();
		style.width = LayoutAxis.stretch();
		style.height = LayoutAxis.fixed(132.0);
		style.clipToParent = true;
		return new CanvasView(key, function(canvas:Canvas, geometry) {
			var background = explorer.state.lightTheme
				? UiExplorer.color(0.91, 0.94, 0.99) : UiExplorer.color(0.07, 0.10, 0.16);
			canvas.fillRoundedRect(new Rect(0.0, 0.0, geometry.width, geometry.height), 10.0, background);
			var inset = 18.0;
			var width = Math.max(32.0, geometry.width - inset * 2.0);
			var height = Math.max(28.0, geometry.height - inset * 2.0);
			var base = new Rect(inset, inset, width, height);
			canvas.fillLinearGradientRect(base, base.x, base.y, base.x + base.width, base.y + base.height, [
				new GradientStop(0.0, Color.rgba(0.12, 0.42, 0.84, 1.0)),
				new GradientStop(0.52, Color.rgba(0.16, 0.72, 0.55, 1.0)),
				new GradientStop(1.0, Color.rgba(0.84, 0.30, 0.55, 1.0))
			]);
			canvas.fillRectIfPositive(new Rect(base.x + width * 0.16, base.y + height * 0.24,
				width * 0.62, height * 0.18), Color.rgba(1.0, 0.88, 0.34, 0.92));
			canvas.fillRectIfPositive(new Rect(base.x + width * 0.38, base.y + height * 0.52,
				width * 0.44, height * 0.24), Color.rgba(0.10, 0.17, 0.32, 0.76));
			canvas.fillRectIfPositive(new Rect(base.x + width * 0.08, base.y + height * 0.72,
				width * 0.28, height * 0.12), Color.rgba(1.0, 1.0, 1.0, 0.82));
		}, style, caption);
	}
}
