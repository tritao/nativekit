package nativekit.ui.core;

/** Shared plotting model with explicit or data-derived ranges. */
class PlotModel {
	final values:Array<PlotSeries>;
	var modelRevision:Int;
	public var minimumX(default, null):Null<Float>;
	public var maximumX(default, null):Null<Float>;
	public var minimumY(default, null):Null<Float>;
	public var maximumY(default, null):Null<Float>;

	public function new() {
		values = [];
		modelRevision = 1;
		minimumX = null;
		maximumX = null;
		minimumY = null;
		maximumY = null;
	}

	public function addSeries(series:PlotSeries):Void {
		if (series == null || seriesById(series.id) != null)
			throw "Plot series IDs must be unique and non-null";
		values.push(series);
		modelRevision++;
	}

	public function removeSeries(id:String):Bool {
		var series = seriesById(id);
		if (series == null)
			return false;
		values.remove(series);
		modelRevision++;
		return true;
	}

	public function series():Array<PlotSeries>
		return values.copy();

	public function seriesById(id:String):Null<PlotSeries> {
		for (series in values)
			if (series.id == id)
				return series;
		return null;
	}

	/** Sets any combination of fixed axis bounds; null keeps that bound data-derived. */
	public function setRange(minimumX:Null<Float>, maximumX:Null<Float>,
			minimumY:Null<Float>, maximumY:Null<Float>):Void {
		validateBound(minimumX, "minimumX");
		validateBound(maximumX, "maximumX");
		validateBound(minimumY, "minimumY");
		validateBound(maximumY, "maximumY");
		if (minimumX != null && maximumX != null && minimumX >= maximumX)
			throw "Plot X range must be increasing";
		if (minimumY != null && maximumY != null && minimumY >= maximumY)
			throw "Plot Y range must be increasing";
		if (this.minimumX == minimumX && this.maximumX == maximumX &&
			this.minimumY == minimumY && this.maximumY == maximumY)
			return;
		this.minimumX = minimumX;
		this.maximumX = maximumX;
		this.minimumY = minimumY;
		this.maximumY = maximumY;
		modelRevision++;
	}

	public function clearRange():Void
		setRange(null, null, null, null);

	/** Includes child series revisions so streaming data invalidates plots. */
	public function revision():Int {
		var result = modelRevision;
		for (series in values)
			result = result * 31 + series.revision;
		return result;
	}

	public function range():Null<PlotRange> {
		var lowX:Null<Float> = minimumX;
		var highX:Null<Float> = maximumX;
		var lowY:Null<Float> = minimumY;
		var highY:Null<Float> = maximumY;
		var deriveLowX = lowX == null;
		var deriveHighX = highX == null;
		var deriveLowY = lowY == null;
		var deriveHighY = highY == null;
		for (series in values)
			if (series.visible)
				for (index in 0...series.pointCount) {
					var point = series.pointAt(index);
					if (deriveLowX)
						lowX = lowX == null ? point.x : Math.min(lowX, point.x);
					if (deriveHighX)
						highX = highX == null ? point.x : Math.max(highX, point.x);
					if (deriveLowY)
						lowY = lowY == null ? point.y : Math.min(lowY, point.y);
					if (deriveHighY)
						highY = highY == null ? point.y : Math.max(highY, point.y);
				}
		if (lowX == null || highX == null || lowY == null || highY == null)
			return null;
		if (deriveLowX && deriveHighX && lowX == highX) {
				lowX -= 0.5;
				highX += 0.5;
		}
		if (deriveLowY && deriveHighY && lowY == highY) {
				lowY -= 0.5;
				highY += 0.5;
		}
		return new PlotRange(lowX, highX, lowY, highY);
	}

	static function validateBound(value:Null<Float>, name:String):Void {
		if (value != null && !finite(value))
			throw 'Plot bound "$name" must be finite';
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
