package nativekit.ui.style;

/** Fluent numeric environment query entry point. */
class EnvironmentDimension {
	final property:Int;

	public function new(property:Int)
		this.property = property;

	public function lessThan(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(property, 0, value);

	public function lessOrEqual(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(property, 1, value);

	public function atLeast(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(property, 3, value);

	public function greaterThan(value:Float):EnvironmentCondition
		return EnvironmentCondition.numeric(property, 4, value);
}
