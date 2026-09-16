package nativekit.ui.theme;

import Color;

/** Semantic palette and spacing tokens consumed by the built-in stylesheet. */
class ThemeTokens {
	public var accent:Color;
	public var text:Color;
	public var mutedText:Color;
	public var disabledText:Color;
	public var buttonText:Color;
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
	public var panelBackground:Color;
	public var overlayBackdrop:Color;
	public var tooltipBackground:Color;

	public function new() {
		accent = Color.rgba(0.22, 0.48, 0.86, 1.0);
		text = Color.rgba(0.96, 0.97, 0.99, 1.0);
		mutedText = Color.rgba(0.69, 0.72, 0.77, 1.0);
		disabledText = Color.rgba(0.53, 0.55, 0.59, 1.0);
		buttonText = text;
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
		panelBackground = Color.rgba(0.13, 0.14, 0.17, 1.0);
		overlayBackdrop = Color.rgba(0.0, 0.0, 0.0, 0.48);
		tooltipBackground = Color.rgba(0.08, 0.09, 0.11, 0.96);
	}
}
