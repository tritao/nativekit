package nativekit.ui.widgets;

import Canvas;
import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import LineCap;
import LineJoin;
import PathBuilder;
import Rect;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CachePolicy;
import nativekit.ui.core.Key;
import nativekit.ui.core.PlotModel;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/** Retained Canvas plot for telemetry, graphs, and simulation diagnostics. */
class PlotView implements View {
	public final key:String;
	public final model:PlotModel;
	public final style:LayoutStyle;
	public var background(default, null):Color;
	public var gridColor(default, null):Color;
	public var axisColor(default, null):Color;
	public var gridDivisions(default, null):Int;
	public var padding(default, null):Float;
	public var enabled:Bool;
	public var label:Null<String>;

	public function new(key:String, model:PlotModel, ?style:LayoutStyle, ?label:String) {
		if (key == null || key.length == 0 || model == null)
			throw "Plots require a stable key and model";
		this.key = key;
		this.model = model;
		this.style = style == null ? defaultStyle() : style.copy();
		background = Color.rgba(0.045, 0.05, 0.065, 1.0);
		gridColor = Color.rgba(0.18, 0.2, 0.24, 0.8);
		axisColor = Color.rgba(0.55, 0.58, 0.64, 0.9);
		gridDivisions = 8;
		padding = 12.0;
		enabled = true;
		this.label = label == null ? "Plot" : label;
	}

	public function setAppearance(background:Color, gridColor:Color, axisColor:Color,
			gridDivisions:Int = 8, padding:Float = 12.0):Void {
		if (background == null || gridColor == null || axisColor == null || gridDivisions < 1 ||
			!finite(padding) || padding < 0.0)
			throw "Plot appearance values are invalid";
		this.background = background;
		this.gridColor = gridColor;
		this.axisColor = axisColor;
		this.gridDivisions = gridDivisions;
		this.padding = padding;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("plot");
			var computed = context.resolveStyle(new StyleTarget("plot", key, key,
				null, ["plot"], context.interactionStates.get(nodeId)), style);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("plot", key, key, null, ["plot"]);
			node.states = context.interactionStates.get(nodeId);
			node.enabled = enabled;
			node.hitTestSelf = enabled;
			node.cachePolicy = CachePolicy.Raster;
			node.computedStyle = computed;
			node.semantics = new Semantics(AccessibilityRole.Group, label == null ? "Plot" : label);
			var cacheKey = "plot:" + model.revision() + ":appearance:" + appearanceKey();
			node.onPaint(function(canvas, geometry) paint(canvas, geometry), cacheKey);
			return node;
		});
	}

	function paint(canvas:Canvas, geometry:ResolvedLayoutItem):Void {
		canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height), background);
		var range = model.range();
		if (range == null || geometry.width <= padding * 2.0 || geometry.height <= padding * 2.0)
			return;
		var left = padding;
		var top = padding;
		var width = geometry.width - padding * 2.0;
		var height = geometry.height - padding * 2.0;
		var divisions = gridDivisions < 1 ? 1 : Std.int(Math.min(gridDivisions, 64));
		var grid = new PathBuilder();
		for (index in 0...(divisions + 1)) {
			var x = left + width * index / divisions;
			var y = top + height * index / divisions;
			grid.moveTo(x, top).lineTo(x, top + height);
			grid.moveTo(left, y).lineTo(left + width, y);
		}
		canvas.strokeTransient(grid.build(), gridColor, 1.0, LineCap.Butt, LineJoin.Miter);
		var axes = new PathBuilder().moveTo(left, top).lineTo(left, top + height)
			.lineTo(left + width, top + height);
		canvas.strokeTransient(axes.build(), axisColor, 1.0, LineCap.Butt, LineJoin.Miter);

		for (series in model.series()) {
			if (!series.visible)
				continue;
			var path = new PathBuilder();
			var started = false;
			for (index in 0...series.pointCount) {
				var point = series.pointAt(index);
				var px = left + (point.x - range.minimumX) /
					(range.maximumX - range.minimumX) * width;
				var py = top + height - (point.y - range.minimumY) /
					(range.maximumY - range.minimumY) * height;
				if (!finite(px) || !finite(py))
					continue;
				if (!started) {
					path.moveTo(px, py);
					started = true;
				} else
					path.lineTo(px, py);
			}
			if (started && series.pointCount > 1)
				canvas.strokeTransient(path.build(), series.color, series.lineWidth,
					LineCap.Round, LineJoin.Round);
		}
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.stretch();
		result.height = LayoutAxis.stretch();
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	function appearanceKey():String
		return background.red + ":" + background.green + ":" + background.blue + ":" + background.alpha +
			":" + gridColor.red + ":" + gridColor.green + ":" + gridColor.blue + ":" + gridColor.alpha +
			":" + axisColor.red + ":" + axisColor.green + ":" + axisColor.blue + ":" + axisColor.alpha +
			":" + gridDivisions + ":" + padding;
}
