package nativekit.ui.style;

/** Runtime capabilities/preferences available to conditional style rules. */
class StyleEnvironment {
	public var width:Float;
	public var height:Float;
	var pixelDensityValue:Float;
	public var pixelDensity(get, set):Float;
	public var orientation:Int;
	var platformValue:String;
	public var platform(get, set):String;
	var colorSchemeValue:Int;
	public var colorScheme(get, set):Int;
	var pointerValue:Int;
	public var pointer(get, set):Int;
	var hoverSupportedValue:Bool;
	public var hoverSupported(get, set):Bool;
	var textScaleValue:Float;
	public var textScale(get, set):Float;
	var reducedMotionValue:Bool;
	public var reducedMotion(get, set):Bool;
	var highContrastValue:Bool;
	public var highContrast(get, set):Bool;
	public var revision(default, null):Int;

	public function new(width:Float = 0.0, height:Float = 0.0) {
		this.width = width;
		this.height = height;
		pixelDensityValue = 1.0;
		orientation = EnvironmentOrientation.Unknown;
		platformValue = "unknown";
		colorSchemeValue = EnvironmentColorScheme.Light;
		pointerValue = EnvironmentPointer.Fine;
		hoverSupportedValue = true;
		textScaleValue = 1.0;
		reducedMotionValue = false;
		highContrastValue = false;
		revision = 0;
		refreshOrientation();
	}

	public function setViewport(width:Float, height:Float):Void {
		if (width <= 0.0 || height <= 0.0)
			throw "Style environment viewport must be positive";
		if (this.width == width && this.height == height)
			return;
		this.width = width;
		this.height = height;
		refreshOrientation();
		revision++;
	}

	function get_pixelDensity():Float return pixelDensityValue;
	function set_pixelDensity(value:Float):Float {
		if (value <= 0.0)
			throw "Style environment pixel density must be positive";
		if (pixelDensityValue != value) {
			pixelDensityValue = value;
			revision++;
		}
		return value;
	}

	function get_platform():String return platformValue;
	function set_platform(value:String):String {
		if (value == null || value.length == 0)
			throw "Style environment platform must be non-empty";
		if (platformValue != value) {
			platformValue = value;
			revision++;
		}
		return value;
	}

	function get_colorScheme():Int return colorSchemeValue;
	function set_colorScheme(value:Int):Int {
		if (value != EnvironmentColorScheme.Light && value != EnvironmentColorScheme.Dark)
			throw "Style environment color scheme is unsupported";
		if (colorSchemeValue != value) {
			colorSchemeValue = value;
			revision++;
		}
		return value;
	}

	function get_pointer():Int return pointerValue;
	function set_pointer(value:Int):Int {
		if (value != EnvironmentPointer.Fine && value != EnvironmentPointer.Coarse)
			throw "Style environment pointer capability is unsupported";
		if (pointerValue != value) {
			pointerValue = value;
			revision++;
		}
		return value;
	}

	function get_hoverSupported():Bool return hoverSupportedValue;
	function set_hoverSupported(value:Bool):Bool {
		if (hoverSupportedValue != value) {
			hoverSupportedValue = value;
			revision++;
		}
		return value;
	}

	function get_textScale():Float return textScaleValue;
	function set_textScale(value:Float):Float {
		if (value <= 0.0)
			throw "Style environment text scale must be positive";
		if (textScaleValue != value) {
			textScaleValue = value;
			revision++;
		}
		return value;
	}

	function get_reducedMotion():Bool return reducedMotionValue;
	function set_reducedMotion(value:Bool):Bool {
		if (reducedMotionValue != value) {
			reducedMotionValue = value;
			revision++;
		}
		return value;
	}

	function get_highContrast():Bool return highContrastValue;
	function set_highContrast(value:Bool):Bool {
		if (highContrastValue != value) {
			highContrastValue = value;
			revision++;
		}
		return value;
	}

	function refreshOrientation():Void
		orientation = width <= 0.0 || height <= 0.0 ? EnvironmentOrientation.Unknown :
			(width >= height ? EnvironmentOrientation.Landscape : EnvironmentOrientation.Portrait);
}
