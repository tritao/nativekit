package nativekit.ui.style;

/** Typed environment query factories; no selector syntax or parser is involved. */
class Environment {
	public static final width:EnvironmentDimension = new EnvironmentDimension(0);
	public static final height:EnvironmentDimension = new EnvironmentDimension(1);
	public static final pixelDensity:EnvironmentDimension = new EnvironmentDimension(2);
	public static final textScale:EnvironmentDimension = new EnvironmentDimension(3);

	public static function widthLessThan(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(0, 0, value);

	public static function widthAtLeast(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(0, 3, value);

	public static function heightLessThan(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(1, 0, value);

	public static function orientation(value:Int):EnvironmentCondition
		return EnvironmentCondition.orientation(value);

	public static function platform(value:String):EnvironmentCondition
		return EnvironmentCondition.platform(value);

	public static function colorScheme(value:Int):EnvironmentCondition
		return EnvironmentCondition.colorScheme(value);

	public static function pointer(value:Int):EnvironmentCondition
		return EnvironmentCondition.pointer(value);

	public static function hoverSupported(value:Bool = true):EnvironmentCondition
		return EnvironmentCondition.hoverSupported(value);

	public static function reducedMotion(value:Bool = true):EnvironmentCondition
		return EnvironmentCondition.reducedMotion(value);

	public static function highContrast(value:Bool = true):EnvironmentCondition
		return EnvironmentCondition.highContrast(value);
}
