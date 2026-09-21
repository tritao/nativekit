package nativekit.ui.core;

import Color;

/** Application-owned samples and presentation for one plot line. */
class PlotSeries {
	public final id:String;
	public final label:String;
	public final color:Color;
	public final lineWidth:Float;
	public var visible(default, null):Bool;
	public var revision(default, null):Int;
	final values:Array<PlotPoint>;

	public function new(id:String, label:String, color:Color, ?lineWidth:Float = 1.5) {
		if (id == null || id.length == 0 || label == null || label.length == 0 ||
			color == null || !finite(lineWidth) || lineWidth <= 0.0)
			throw "Plot series require stable labels, a color, and positive line width";
		this.id = id;
		this.label = label;
		this.color = color;
		this.lineWidth = lineWidth;
		visible = true;
		revision = 1;
		values = [];
	}

	public function points():Array<PlotPoint>
		return values.copy();

	public var pointCount(get, never):Int;
	inline function get_pointCount():Int
		return values.length;

	public function pointAt(index:Int):PlotPoint {
		if (index < 0 || index >= values.length)
			throw "Plot point index is out of range";
		return values[index];
	}

	public function setVisible(value:Bool):Bool {
		if (visible == value)
			return false;
		visible = value;
		revision++;
		return true;
	}

	public function setPoints(points:Array<PlotPoint>):Void {
		values.resize(0);
		if (points != null)
			for (point in points) {
				if (point == null)
					throw "Plot series cannot contain null points";
				values.push(point);
			}
		revision++;
	}

	public function add(point:PlotPoint):Void {
		if (point == null)
			throw "Plot series cannot contain null points";
		values.push(point);
		revision++;
	}

	public function clear():Void {
		if (values.length == 0)
			return;
		values.resize(0);
		revision++;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
