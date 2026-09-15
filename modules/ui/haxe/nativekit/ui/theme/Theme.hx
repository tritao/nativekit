package nativekit.ui.theme;

import Color;
import LayoutStyle;
import ParagraphStyle;
import TextStyle;

/** Haxe-owned color and state palette resolved before the layout transaction. */
class Theme {
	static final darkButtonTextOnLight:Color = Color.rgba(0.08, 0.10, 0.14, 1.0);

	public var accent:Color;
	public var text:Color;
	public var mutedText:Color;
	public var disabledText:Color;
	public var buttonText:Color;
	public var disabledButtonText:Color;
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
	public var textStyle:TextStyle;
	public var paragraphStyle:ParagraphStyle;

	public function new() {
		accent = Color.rgba(0.22, 0.48, 0.86, 1.0);
		text = Color.rgba(0.96, 0.97, 0.99, 1.0);
		mutedText = Color.rgba(0.69, 0.72, 0.77, 1.0);
		disabledText = Color.rgba(0.53, 0.55, 0.59, 1.0);
		buttonText = text;
		disabledButtonText = disabledText;
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
		textStyle = new TextStyle();
		paragraphStyle = new ParagraphStyle();
	}

	/** Applies state colors while preserving caller-provided normal layout styling. */
	public function resolveButtonStyle(base:LayoutStyle, flags:Int, enabled:Bool):LayoutStyle {
		var result = base.copy();
		if (!enabled)
			result.background = buttonDisabled;
		else if (InteractionState.contains(flags, InteractionState.Pressed))
			result.background = buttonPressed;
		else if (InteractionState.contains(flags, InteractionState.Hovered))
			result.background = buttonHover;
		else if (InteractionState.contains(flags, InteractionState.Focused))
			result.background = buttonFocused;
		else if (InteractionState.contains(flags, InteractionState.Selected))
			result.background = buttonSelected;
		return result;
	}

	public function textColor(enabled:Bool):Color
		return enabled ? text : disabledText;

	/** Chooses a readable foreground for both accent-filled and light neutral buttons. */
	public function buttonLabelColor(enabled:Bool, background:Color):Color {
		if (!enabled)
			return disabledButtonText;
		if (background == null || background.alpha < 0.5)
			return text;
		var luminance = channelLuminance(background.red) * 0.2126 +
			channelLuminance(background.green) * 0.7152 +
			channelLuminance(background.blue) * 0.0722;
		if (luminance > 0.179)
			return channelLuminance(text.red) * 0.2126 + channelLuminance(text.green) * 0.7152 +
				channelLuminance(text.blue) * 0.0722 <= 0.179 ? text : darkButtonTextOnLight;
		return buttonText;
	}

	static inline function channelLuminance(channel:Float):Float
		return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);

	public function controlColor(selected:Bool, enabled:Bool):Color {
		if (!enabled)
			return controlDisabled;
		return selected ? controlSelected : controlUnselected;
	}
}
