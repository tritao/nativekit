package nativekit.ui.style;

/** Runtime capabilities/preferences available to conditional style rules. */
class StyleEnvironment {
	public var width:Float;
	public var height:Float;
	public var pixelDensity:Float;
	public var orientation:Int;
	public var platform:String;
	public var colorScheme:Int;
	public var pointer:Int;
	public var hoverSupported:Bool;
	public var textScale:Float;
	public var reducedMotion:Bool;
	public var highContrast:Bool;
	public var revision(default, null):Int;

	public function new(width:Float = 0.0, height:Float = 0.0) {
		this.width = width;
		this.height = height;
		pixelDensity = 1.0;
		orientation = EnvironmentOrientation.Unknown;
		platform = "unknown";
		colorScheme = EnvironmentColorScheme.Light;
		pointer = EnvironmentPointer.Fine;
		hoverSupported = true;
		textScale = 1.0;
		reducedMotion = false;
		highContrast = false;
		revision = 0;
		refreshOrientation();
	}

	public function setViewport(width:Float, height:Float):Void {
		if (width <= 0.0 || height <= 0.0)
			throw "Style environment viewport must be positive";
		this.width = width;
		this.height = height;
		refreshOrientation();
		revision++;
	}

	function refreshOrientation():Void
		orientation = width <= 0.0 || height <= 0.0 ? EnvironmentOrientation.Unknown :
			(width >= height ? EnvironmentOrientation.Landscape : EnvironmentOrientation.Portrait);
}
