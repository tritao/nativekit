package nativekit.ui.theme;

import Color;
import Insets;
import LayoutStyle;
import ParagraphStyle;
import TextStyle;
import LayoutAlignment;
import LayoutAxis;
import LayoutAxis;
import nativekit.ui.style.StyleSheet;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleValue;

/** Theme tokens plus the stylesheet generated from those tokens. */
class Theme {
	static final darkButtonTextOnLight:Color = Color.rgba(0.08, 0.10, 0.14, 1.0);
	public final tokens:ThemeTokens;
	public final styles:StyleSheet;

	public var textSelection:Color;
	public var textSelectionInactive:Color;
	public var textCaret:Color;
	public var body:TextRoleStyle;
	public var heading:TextRoleStyle;
	public var label:TextRoleStyle;
	public var caption:TextRoleStyle;
	public var button:TextRoleStyle;
	public var accent(get, set):Color;
	public var text(get, set):Color;
	public var mutedText(get, set):Color;
	public var disabledText(get, set):Color;
	public var buttonText(get, set):Color;
	public var disabledButtonText(get, set):Color;
	public var buttonBackground(get, set):Color;
	public var buttonHover(get, set):Color;
	public var buttonPressed(get, set):Color;
	public var buttonFocused(get, set):Color;
	public var buttonSelected(get, set):Color;
	public var buttonDisabled(get, set):Color;
	public var controlSelected(get, set):Color;
	public var controlUnselected(get, set):Color;
	public var controlDisabled(get, set):Color;
	public var panelBackground(get, set):Color;
	public var overlayBackdrop(get, set):Color;
	public var tooltipBackground(get, set):Color;

	public function new(?tokens:ThemeTokens) {
		this.tokens = tokens == null ? new ThemeTokens() : tokens;
		styles = new StyleSheet("Theme");
		textSelection = Color.rgba(0.2, 0.43, 0.82, 0.55);
		textSelectionInactive = Color.rgba(0.2, 0.43, 0.82, 0.30);
		textCaret = Color.rgba(0.96, 0.97, 0.99, 1.0);
		body = new TextRoleStyle(new TextStyle(), new ParagraphStyle(), tokens.text);
		heading = new TextRoleStyle(new TextStyle(24.0), new ParagraphStyle(), tokens.text);
		label = new TextRoleStyle(new TextStyle(14.0),
			new ParagraphStyle(TextWrap.None), tokens.text);
		caption = new TextRoleStyle(new TextStyle(12.0), new ParagraphStyle(), tokens.mutedText);
		button = new TextRoleStyle(new TextStyle(),
			new ParagraphStyle(TextWrap.None), tokens.text);
		refreshStyles();
	}

	/** Rebuilds built-in rules after callers change a compatibility token. */
	public function refreshStyles():Void {
		styles.clear();
		styles.rule(StyleSelector.widget("button"), [
			StyleValue.background(buttonBackground),
			StyleValue.paddingSymmetric(tokens.spacingLarge, tokens.spacingMedium),
			StyleValue.radius(StyleProperty.RadiusTopLeft, tokens.radiusMedium),
			StyleValue.radius(StyleProperty.RadiusTopRight, tokens.radiusMedium),
			StyleValue.radius(StyleProperty.RadiusBottomRight, tokens.radiusMedium),
			StyleValue.radius(StyleProperty.RadiusBottomLeft, tokens.radiusMedium)
		]);
		styles.rule(StyleSelector.widget("button").state(StyleState.Selected),
			[StyleValue.background(buttonSelected)]);
		styles.rule(StyleSelector.widget("button").state(StyleState.Focused),
			[StyleValue.background(buttonFocused)]);
		styles.rule(StyleSelector.widget("button").state(StyleState.Hovered),
			[StyleValue.background(buttonHover)]);
		styles.rule(StyleSelector.widget("button").state(StyleState.Pressed),
			[StyleValue.background(buttonPressed)]);
		styles.rule(StyleSelector.widget("button").state(StyleState.Disabled),
			[StyleValue.background(buttonDisabled)]);
		styles.rule(StyleSelector.widget("button").className("menu-item"), [
			StyleValue.width(LayoutAxis.grow()),
			StyleValue.padding(new Insets(10.0, 10.0, 6.0, 6.0)),
			StyleValue.background(Color.rgba(0.12, 0.13, 0.16, 0.0))
		]);
		styles.rule(StyleSelector.widget("button").className("menu-item").state(StyleState.Disabled),
			[StyleValue.background(Color.rgba(0.12, 0.13, 0.16, 0.45))]);
		styles.rule(StyleSelector.widget("text-field"), [StyleValue.textColor(text)]);
		styles.rule(StyleSelector.widget("text-field").state(StyleState.Disabled),
			[StyleValue.textColor(disabledText)]);
		styles.rule(StyleSelector.widget("checkbox"), [StyleValue.textColor(text)]);
		styles.rule(StyleSelector.widget("checkbox").state(StyleState.Disabled),
			[StyleValue.textColor(disabledText)]);
		styles.rule(StyleSelector.widget("toggle"), [StyleValue.textColor(text)]);
		styles.rule(StyleSelector.widget("toggle").state(StyleState.Disabled),
			[StyleValue.textColor(disabledText)]);
		styles.rule(StyleSelector.widget("radio"), [StyleValue.textColor(text)]);
		styles.rule(StyleSelector.widget("radio").state(StyleState.Disabled),
			[StyleValue.textColor(disabledText)]);

		styles.rule(StyleSelector.widget("checkbox-indicator"),
			[StyleValue.background(controlUnselected)]);
		styles.rule(StyleSelector.widget("checkbox-indicator").state(StyleState.Checked),
			[StyleValue.background(controlSelected)]);
		styles.rule(StyleSelector.widget("checkbox-indicator").state(StyleState.Disabled),
			[StyleValue.background(controlDisabled)]);

		styles.rule(StyleSelector.widget("toggle-indicator"), [
			StyleValue.background(controlUnselected), StyleValue.alignX(LayoutAlignment.Start)]);
		styles.rule(StyleSelector.widget("toggle-indicator").state(StyleState.Checked), [
			StyleValue.background(controlSelected), StyleValue.alignX(LayoutAlignment.End)]);
		styles.rule(StyleSelector.widget("toggle-indicator").state(StyleState.Disabled),
			[StyleValue.background(controlDisabled)]);
		styles.rule(StyleSelector.widget("toggle-thumb"),
			[StyleValue.background(Color.rgba(0.98, 0.98, 0.99, 1.0))]);

		styles.rule(StyleSelector.widget("radio-indicator"), [StyleValue.background(controlUnselected)]);
		styles.rule(StyleSelector.widget("radio-indicator").state(StyleState.Selected),
			[StyleValue.background(controlSelected)]);
		styles.rule(StyleSelector.widget("radio-indicator").state(StyleState.Disabled),
			[StyleValue.background(controlDisabled)]);
		styles.rule(StyleSelector.widget("radio-dot"),
			[StyleValue.background(Color.rgba(0.0, 0.0, 0.0, 0.0))]);
		styles.rule(StyleSelector.widget("radio-dot").state(StyleState.Selected),
			[StyleValue.background(controlSelected)]);

		styles.rule(StyleSelector.widget("slider"), [
			StyleValue.sliderTrackColor(controlUnselected),
			StyleValue.sliderFillColor(accent),
			StyleValue.sliderThumbColor(text)
		]);
		styles.rule(StyleSelector.widget("slider").state(StyleState.Disabled), [
			StyleValue.sliderTrackColor(controlDisabled),
			StyleValue.sliderFillColor(controlDisabled),
			StyleValue.sliderThumbColor(disabledText)
		]);
		styles.rule(StyleSelector.widget("progress-bar"), [
			StyleValue.progressTrackColor(Color.rgba(0.19, 0.21, 0.25, 1.0)),
			StyleValue.progressFillColor(accent)
		]);
		styles.rule(StyleSelector.widget("popup-content"), [
			StyleValue.padding(new Insets(8.0, 8.0, 8.0, 8.0)),
			StyleValue.background(panelBackground),
			StyleValue.radius(StyleProperty.RadiusTopLeft, 5.0),
			StyleValue.radius(StyleProperty.RadiusTopRight, 5.0),
			StyleValue.radius(StyleProperty.RadiusBottomRight, 5.0),
			StyleValue.radius(StyleProperty.RadiusBottomLeft, 5.0)
		]);
		styles.rule(StyleSelector.widget("popup-backdrop"),
			[StyleValue.background(overlayBackdrop)]);
		styles.rule(StyleSelector.widget("dialog-backdrop"),
			[StyleValue.background(overlayBackdrop)]);
		styles.rule(StyleSelector.widget("dialog-panel"), [
			StyleValue.padding(new Insets(24.0, 24.0, 24.0, 24.0)),
			StyleValue.background(panelBackground),
			StyleValue.radius(StyleProperty.RadiusTopLeft, 8.0),
			StyleValue.radius(StyleProperty.RadiusTopRight, 8.0),
			StyleValue.radius(StyleProperty.RadiusBottomRight, 8.0),
			StyleValue.radius(StyleProperty.RadiusBottomLeft, 8.0),
			StyleValue.of(StyleProperty.ChildGap, 16.0)
		]);
		styles.rule(StyleSelector.widget("dialog-heading"),
			[StyleValue.textColor(text)]);
		styles.rule(StyleSelector.widget("tooltip"), [
			StyleValue.padding(new Insets(6.0, 6.0, 4.0, 4.0)),
			StyleValue.background(tooltipBackground),
			StyleValue.radius(StyleProperty.RadiusTopLeft, tokens.radiusSmall),
			StyleValue.radius(StyleProperty.RadiusTopRight, tokens.radiusSmall),
			StyleValue.radius(StyleProperty.RadiusBottomRight, tokens.radiusSmall),
			StyleValue.radius(StyleProperty.RadiusBottomLeft, tokens.radiusSmall)
		]);
	}

	public function textColor(enabled:Bool):Color
		return textRoleColor(TextRole.Body, enabled);

	/** Resolves a role's normal color while applying the shared disabled state. */
	public function textRoleColor(role:TextRole, enabled:Bool):Color
		return enabled ? textRole(role).color : disabledText;

	/** Chooses a readable foreground for both accent-filled and light neutral buttons. */
	public function buttonLabelColor(enabled:Bool, background:Color):Color {
		if (!enabled)
			return disabledButtonText;
		if (background == null || background.alpha < 0.5)
			return body.color;
		var luminance = channelLuminance(background.red) * 0.2126 +
			channelLuminance(background.green) * 0.7152 +
			channelLuminance(background.blue) * 0.0722;
		if (luminance > 0.179)
			return channelLuminance(body.color.red) * 0.2126 + channelLuminance(body.color.green) * 0.7152 +
				channelLuminance(body.color.blue) * 0.0722 <= 0.179 ? body.color : darkButtonTextOnLight;
		return button.color;
	}

	/** Returns the complete concrete style associated with a semantic role. */
	public function textRole(role:TextRole):TextRoleStyle {
		if (role == TextRole.Heading)
			return heading;
		if (role == TextRole.Label)
			return label;
		if (role == TextRole.Caption)
			return caption;
		if (role == TextRole.Button)
			return button;
		return body;
	}

	static inline function channelLuminance(channel:Float):Float
		return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);

	public function controlColor(selected:Bool, enabled:Bool):Color {
		if (!enabled)
			return controlDisabled;
		return selected ? controlSelected : controlUnselected;
	}

	function get_accent():Color return tokens.accent;
	function set_accent(value:Color):Color { tokens.accent = value; return value; }
	function get_text():Color return body == null ? tokens.text : body.color;
	function set_text(value:Color):Color {
		tokens.text = value;
		if (body != null)
			body.color = value;
		return value;
	}
	function get_mutedText():Color return caption == null ? tokens.mutedText : caption.color;
	function set_mutedText(value:Color):Color {
		tokens.mutedText = value;
		if (caption != null)
			caption.color = value;
		return value;
	}
	function get_disabledText():Color return tokens.disabledText;
	function set_disabledText(value:Color):Color { tokens.disabledText = value; return value; }
	function get_buttonText():Color return button == null ? tokens.buttonText : button.color;
	function set_buttonText(value:Color):Color {
		tokens.buttonText = value;
		if (button != null)
			button.color = value;
		return value;
	}
	function get_disabledButtonText():Color return tokens.disabledButtonText;
	function set_disabledButtonText(value:Color):Color { tokens.disabledButtonText = value; return value; }
	function get_buttonBackground():Color return tokens.buttonBackground;
	function set_buttonBackground(value:Color):Color { tokens.buttonBackground = value; return value; }
	function get_buttonHover():Color return tokens.buttonHover;
	function set_buttonHover(value:Color):Color { tokens.buttonHover = value; return value; }
	function get_buttonPressed():Color return tokens.buttonPressed;
	function set_buttonPressed(value:Color):Color { tokens.buttonPressed = value; return value; }
	function get_buttonFocused():Color return tokens.buttonFocused;
	function set_buttonFocused(value:Color):Color { tokens.buttonFocused = value; return value; }
	function get_buttonSelected():Color return tokens.buttonSelected;
	function set_buttonSelected(value:Color):Color { tokens.buttonSelected = value; return value; }
	function get_buttonDisabled():Color return tokens.buttonDisabled;
	function set_buttonDisabled(value:Color):Color { tokens.buttonDisabled = value; return value; }
	function get_controlSelected():Color return tokens.controlSelected;
	function set_controlSelected(value:Color):Color { tokens.controlSelected = value; return value; }
	function get_controlUnselected():Color return tokens.controlUnselected;
	function set_controlUnselected(value:Color):Color { tokens.controlUnselected = value; return value; }
	function get_controlDisabled():Color return tokens.controlDisabled;
	function set_controlDisabled(value:Color):Color { tokens.controlDisabled = value; return value; }
	function get_panelBackground():Color return tokens.panelBackground;
	function set_panelBackground(value:Color):Color { tokens.panelBackground = value; return value; }
	function get_overlayBackdrop():Color return tokens.overlayBackdrop;
	function set_overlayBackdrop(value:Color):Color { tokens.overlayBackdrop = value; return value; }
	function get_tooltipBackground():Color return tokens.tooltipBackground;
	function set_tooltipBackground(value:Color):Color { tokens.tooltipBackground = value; return value; }
}
