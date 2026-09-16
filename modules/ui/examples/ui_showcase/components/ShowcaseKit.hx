package components;

import Color;
import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import UiExplorer;
import nativekit.ui.core.View;
import nativekit.ui.theme.Theme;
import nativekit.ui.theme.TextRole;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleValue;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Padding;
import nativekit.ui.widgets.Slider;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TextArea;
import nativekit.ui.widgets.TextField;

/** Showcase-only styling and small compositions built from framework widgets. */
@:allow(UiExplorer)
class ShowcaseKit {
	static function textField(explorer:UiExplorer):TextField {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(42.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = explorer.state.lightTheme
			? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextField("demo-name", explorer.state.controls.nameValue, function(value) {
			explorer.state.controls.nameValue = value;
		}, style, "Display name");
	}

	static function textArea(explorer:UiExplorer):TextArea {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(146.0);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = explorer.state.lightTheme
			? color(0.92, 0.94, 0.98) : color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new TextArea("demo-notes", explorer.state.controls.notesValue, function(value) {
			explorer.state.controls.notesValue = value;
		}, style, "Multilingual notes");
	}

	static function slider(explorer:UiExplorer):Slider {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(36.0);
		return new Slider("volume-slider", "Volume", explorer.state.controls.volume,
			0.0, 1.0, 0.01, function(value) { explorer.state.controls.volume = value; }, style);
	}

	static function button(explorer:UiExplorer, label:String, key:String,
			action:Void->Void, selected:Bool = false):Button {
		var style = new LayoutStyle();
		style.height = LayoutAxis.fixed(38.0);
		style.padding = new Insets(12.0, 9.0, 12.0, 9.0);
		style.background = explorer.state.lightTheme
			? color(0.18, 0.39, 0.70) : color(0.16, 0.38, 0.70);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		var result = new Button(label, style, action, key);
		result.selected = selected;
		return result;
	}

	static function disabledButton(explorer:UiExplorer, label:String):Button {
		var result = button(explorer, label, "disabled-demo", function() {});
		result.enabled = false;
		return result;
	}

	static function panel(explorer:UiExplorer, key:String,
			children:Array<KeyedView>):Column
		return new Column(key, children, panelStyle(explorer));

	static function panelStyle(explorer:UiExplorer, ?fixedWidth:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = fixedWidth == null ? LayoutAxis.grow() : LayoutAxis.fixed(fixedWidth);
		style.height = LayoutAxis.fit();
		style.padding = new Insets(16.0, 14.0, 16.0, 14.0);
		style.childGap = 10.0;
		style.background = explorer.state.lightTheme
			? color(0.97, 0.98, 1.0) : color(0.10, 0.14, 0.22);
		style.radiusTopLeft = style.radiusTopRight = 7.0;
		style.radiusBottomLeft = style.radiusBottomRight = 7.0;
		return style;
	}

	static function columnStyle(explorer:UiExplorer, width:Float,
			height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width);
		style.height = LayoutAxis.fixed(height);
		style.padding = new Insets(20.0, 18.0, 20.0, 18.0);
		style.childGap = 12.0;
		style.background = panelStyle(explorer).background;
		return style;
	}

	static function rowStyle(gap:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.direction = LayoutDirection.LeftToRight;
		style.childGap = gap;
		return style;
	}

	static function fixedBoxStyle(explorer:UiExplorer, width:Float,
			height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width);
		style.height = LayoutAxis.fixed(height);
		style.padding = new Insets(10.0, 10.0, 10.0, 10.0);
		style.background = explorer.state.lightTheme
			? color(0.88, 0.91, 0.96) : color(0.07, 0.10, 0.16);
		return style;
	}

	static function colorTile(label:String, background:Color, growWeight:Float = 1.0):View {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow(0.0, 0.0, growWeight);
		style.height = LayoutAxis.fixed(50.0);
		style.padding = new Insets(10.0, 10.0, 10.0, 10.0);
		style.background = background;
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return new Padding("tile-padding", new Text(label, null, color(1.0, 1.0, 1.0)),
			new Insets(10.0, 10.0, 10.0, 10.0), style);
	}

	static function stackDemo(explorer:UiExplorer):View {
		var rootStyle = fixedBoxStyle(explorer, 520.0, 122.0);
		rootStyle.width = LayoutAxis.grow();
		var layers:Array<StackChild> = [
			new StackChild("base", colorTile("base layer", color(0.16, 0.29, 0.45)),
				12.0, 12.0, 0, LayoutAxis.fixed(250.0), LayoutAxis.fixed(72.0)),
			new StackChild("top", colorTile("z-index 1", color(0.38, 0.26, 0.61)),
				190.0, 36.0, 1, LayoutAxis.fixed(220.0), LayoutAxis.fixed(68.0))
		];
		return new Stack("positioned-stack", layers, rootStyle);
	}

	static function keyed(key:String, view:View):KeyedView
		return new KeyedView(key, view);

	static function text(value:String, ?color:Color, role:TextRole = TextRole.Body):Text
		return new Text(value, null, color, null, role);

	static function heading(value:String):Text
		return text(value, null, TextRole.Heading);

	static function caption(value:String):Text
		return text(value, null, TextRole.Caption);

	static function label(value:String):Text
		return text(value, null, TextRole.Label);

	static function paletteBackground(explorer:UiExplorer):Color
		return explorer.state.lightTheme
			? color(0.93, 0.95, 0.98) : color(0.065, 0.085, 0.13);

	static function paletteSidebar(explorer:UiExplorer):Color
		return explorer.state.lightTheme
			? color(0.88, 0.91, 0.96) : color(0.08, 0.11, 0.17);

	static function makeTheme(light:Bool):Theme {
		var theme = new Theme();
		theme.accent = light ? color(0.12, 0.37, 0.72) : color(0.25, 0.61, 0.89);
		theme.body.color = light ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);
		theme.heading.color = theme.body.color;
		theme.label.color = theme.body.color;
		theme.caption.color = light ? color(0.32, 0.38, 0.47) : color(0.62, 0.68, 0.77);
		theme.button.color = color(1.0, 1.0, 1.0);
		theme.disabledButtonText = light ? color(0.38, 0.41, 0.46) : color(0.53, 0.55, 0.59);
		theme.buttonHover = light ? color(0.16, 0.38, 0.69) : color(0.22, 0.48, 0.82);
		theme.buttonPressed = light ? color(0.11, 0.29, 0.54) : color(0.13, 0.34, 0.67);
		theme.buttonFocused = light ? color(0.22, 0.43, 0.73) : color(0.27, 0.52, 0.91);
		theme.buttonSelected = light ? color(0.16, 0.36, 0.65) : color(0.17, 0.37, 0.68);
		theme.buttonDisabled = light ? color(0.82, 0.84, 0.88) : color(0.22, 0.24, 0.28);
		theme.controlSelected = theme.accent;
		theme.controlUnselected = light ? color(0.78, 0.81, 0.86) : color(0.16, 0.18, 0.22);
		theme.controlDisabled = light ? color(0.82, 0.84, 0.88) : color(0.20, 0.21, 0.24);
		theme.textCaret = light ? color(0.10, 0.14, 0.21) : color(0.91, 0.94, 0.98);
		theme.panelBackground = light ? color(0.98, 0.98, 1.0) : color(0.14, 0.16, 0.20);
		theme.overlayBackdrop = color(0.0, 0.0, 0.0, 0.54);
		theme.tooltipBackground = light ? color(0.13, 0.17, 0.23) : color(0.08, 0.09, 0.11);
		theme.refreshStyles();
		var catalogNormal = light ? color(0.87, 0.90, 0.95) : color(0.075, 0.10, 0.16);
		var catalogHover = light ? color(0.79, 0.85, 0.94) : color(0.12, 0.18, 0.28);
		var catalogPressed = light ? color(0.72, 0.81, 0.92) : color(0.15, 0.23, 0.36);
		var catalogSelected = light ? color(0.74, 0.83, 0.95) : color(0.16, 0.29, 0.50);
		theme.styles.rule(StyleSelector.widget("button").className("catalog-nav"),
			[StyleValue.background(catalogNormal)]);
		theme.styles.rule(StyleSelector.widget("button").className("catalog-nav")
			.state(StyleState.Hovered), [StyleValue.background(catalogHover)]);
		theme.styles.rule(StyleSelector.widget("button").className("catalog-nav")
			.state(StyleState.Pressed), [StyleValue.background(catalogPressed)]);
		theme.styles.rule(StyleSelector.widget("button").className("catalog-nav")
			.state(StyleState.Selected), [StyleValue.background(catalogSelected)]);
		return theme;
	}

	static inline function color(red:Float, green:Float, blue:Float,
			alpha:Float = 1.0):Color
		return Color.rgba(red, green, blue, alpha);
}
