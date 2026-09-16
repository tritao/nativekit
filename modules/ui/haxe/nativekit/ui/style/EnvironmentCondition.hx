package nativekit.ui.style;

/** Typed environment predicate used by conditional style rules. */
class EnvironmentCondition {
	public static inline var Width:Int = 0;
	public static inline var Height:Int = 1;
	public static inline var Density:Int = 2;
	public static inline var TextScale:Int = 3;
	public static inline var Orientation:Int = 4;
	public static inline var Platform:Int = 5;
	public static inline var ColorScheme:Int = 6;
	public static inline var Pointer:Int = 7;
	public static inline var HoverSupport:Int = 8;
	public static inline var ReducedMotion:Int = 9;
	public static inline var HighContrast:Int = 10;
	public static inline var Less:Int = 0;
	public static inline var LessOrEqual:Int = 1;
	public static inline var Equal:Int = 2;
	public static inline var GreaterOrEqual:Int = 3;
	public static inline var Greater:Int = 4;

	final property:Int;
	final comparison:Int;
	final number:Float;
	final text:String;
	final flag:Bool;

	function new(property:Int, comparison:Int, number:Float, text:String, flag:Bool) {
		this.property = property;
		this.comparison = comparison;
		this.number = number;
		this.text = text;
		this.flag = flag;
	}

	public function matches(environment:StyleEnvironment):Bool {
		if (environment == null)
			return false;
		if (property == 0) return compare(environment.width);
		if (property == 1) return compare(environment.height);
		if (property == 2) return compare(environment.pixelDensity);
		if (property == 3) return compare(environment.textScale);
		if (property == 4) return compare(cast environment.orientation);
		if (property == 5) return environment.platform == text;
		if (property == 6) return compare(cast environment.colorScheme);
		if (property == 7) return compare(cast environment.pointer);
		if (property == 8) return environment.hoverSupported == flag;
		if (property == 9) return environment.reducedMotion == flag;
		if (property == 10) return environment.highContrast == flag;
		return false;
	}

	function compare(value:Float):Bool {
		if (comparison == 0) return value < number;
		if (comparison == 1) return value <= number;
		if (comparison == 2) return value == number;
		if (comparison == 3) return value >= number;
		if (comparison == 4) return value > number;
		return false;
	}

	public static function numeric(property:Int, comparison:Int, value:Float):EnvironmentCondition
		return new EnvironmentCondition(property, comparison, value, "", false);

	public static function enumValue(property:Int, value:Int):EnvironmentCondition
		return new EnvironmentCondition(property, 2, value, "", false);

	public static function boolean(property:Int, value:Bool):EnvironmentCondition
		return new EnvironmentCondition(property, 2, 0.0, "", value);

	public static function orientation(value:Int):EnvironmentCondition
		return enumValue(4, value);

	public static function platform(value:String):EnvironmentCondition
		return new EnvironmentCondition(5, 2, 0.0, value, false);

	public static function colorScheme(value:Int):EnvironmentCondition
		return enumValue(6, value);

	public static function pointer(value:Int):EnvironmentCondition
		return enumValue(7, value);

	public static function hoverSupported(value:Bool = true):EnvironmentCondition
		return boolean(8, value);

	public static function reducedMotion(value:Bool = true):EnvironmentCondition
		return boolean(9, value);

	public static function highContrast(value:Bool = true):EnvironmentCondition
		return boolean(10, value);
}
