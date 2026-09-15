/** Adapt a canvas, native view, editor, or other host surface to layout measurement. */
class LayoutMeasuredContent implements LayoutContent {
	final callback:LayoutContentMeasure;
	var versionValue:Int;

	public function new(callback:LayoutContentMeasure, version:Int = 0) {
		if (callback == null || version < 0)
			throw "Measured content requires a callback and non-negative version";
		this.callback = callback;
		versionValue = version;
	}

	public function getVersion():Int
		return versionValue;

	/** Marks host content as changed without replacing the provider. */
	public function invalidate():Void {
		if (versionValue == 0x7fffffff)
			throw "Measured content version exhausted";
		versionValue++;
	}

	/** Sets an externally managed content version. */
	public function setVersion(version:Int):Void {
		if (version < 0)
			throw "Measured content version must be non-negative";
		versionValue = version;
	}

	public function measure(constraints:LayoutMeasureConstraints):LayoutMeasureResult
		return callback(constraints);
}

typedef LayoutContentMeasure = (constraints:LayoutMeasureConstraints) -> LayoutMeasureResult;
