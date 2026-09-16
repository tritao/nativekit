package nativekit.ui.theme;

import Color;
import LayoutStyle;
import ParagraphStyle;
import TextStyle;
import nativekit.ui.style.StyleSheet;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleValue;

/** Haxe-owned semantic typography, color, and state palette. */
class Theme {
	static final darkButtonTextOnLight:Color = Color.rgba(0.08, 0.10, 0.14, 1.0);
	public final styles:StyleSheet;

	public var accent:Color;
	public var disabledText:Color;
	public var disabledButtonText:Color;
	public var buttonBackground:Color;
	public var buttonHover:Color;
	public var buttonPressed:Color;
	public var buttonFocused:Color;
	public var buttonSelected:Color;
	public var buttonDisabled:Color;
	public var controlSelected:Color;
	public var controlUnselected:Color;
	public var controlDisabled:Color;
	public var textSelection:Color;
	public var textSelectionInactive:Color;
	public var textCaret:Color;
	public var panelBackground:Color;
	public var overlayBackdrop:Color;
	public var tooltipBackground:Color;
	public var body:TextRoleStyle;
	public var heading:TextRoleStyle;
	public var label:TextRoleStyle;
	public var caption:TextRoleStyle;
	public var button:TextRoleStyle;

	public function new() {
		styles = new StyleSheet("Theme");
		accent = Color.rgba(0.22, 0.48, 0.86, 1.0);
		var bodyColor = Color.rgba(0.96, 0.97, 0.99, 1.0);
		var mutedColor = Color.rgba(0.69, 0.72, 0.77, 1.0);
		disabledText = Color.rgba(0.53, 0.55, 0.59, 1.0);
		disabledButtonText = disabledText;
		buttonBackground = Color.rgba(0.16, 0.4, 0.78, 1.0);
		buttonHover = Color.rgba(0.21, 0.46, 0.84, 1.0);
		buttonPressed = Color.rgba(0.13, 0.34, 0.67, 1.0);
		buttonFocused = Color.rgba(0.27, 0.52, 0.91, 1.0);
		buttonSelected = Color.rgba(0.17, 0.37, 0.68, 1.0);
		buttonDisabled = Color.rgba(0.22, 0.24, 0.28, 1.0);
		controlSelected = accent;
		controlUnselected = Color.rgba(0.16, 0.18, 0.22, 1.0);
		controlDisabled = Color.rgba(0.20, 0.21, 0.24, 1.0);
		textSelection = Color.rgba(0.2, 0.43, 0.82, 0.55);
		textSelectionInactive = Color.rgba(0.2, 0.43, 0.82, 0.30);
		textCaret = Color.rgba(0.96, 0.97, 0.99, 1.0);
		panelBackground = Color.rgba(0.13, 0.14, 0.17, 1.0);
		overlayBackdrop = Color.rgba(0.0, 0.0, 0.0, 0.48);
		tooltipBackground = Color.rgba(0.08, 0.09, 0.11, 0.96);
		body = new TextRoleStyle(new TextStyle(), new ParagraphStyle(), bodyColor);
		heading = new TextRoleStyle(new TextStyle(24.0), new ParagraphStyle(), bodyColor);
		label = new TextRoleStyle(new TextStyle(14.0),
			new ParagraphStyle(TextWrap.None), bodyColor);
		caption = new TextRoleStyle(new TextStyle(12.0), new ParagraphStyle(), mutedColor);
		button = new TextRoleStyle(new TextStyle(),
			new ParagraphStyle(TextWrap.None), bodyColor);
		refreshStyles();
	}

	/** Rebuilds the built-in rules after callers change a compatibility token. */
	public function refreshStyles():Void {
		styles.clear();
		styles.rule(StyleSelector.widget("button"), [StyleValue.background(buttonBackground)]);
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
}
