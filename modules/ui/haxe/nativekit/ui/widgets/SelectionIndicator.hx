package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import LineCap;
import LineJoin;
import PathBuilder;
import Rect;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.style.StyleProperty;

/** Small font-independent affordances shared by selection controls. */
class SelectionIndicator {
	public static function fieldFrame(context:BuildContext, node:RenderNode, key:String):Void {
		if (node.computedStyle == null)
			return;
		var color = node.computedStyle.get(StyleProperty.BorderColor);
		var width = node.computedStyle.get(StyleProperty.BorderWidth);
		if (color.alpha <= 0.0 || width <= 0.0)
			return;
		var indicator = overlay(context, key);
		indicator.onPaint(function(canvas, geometry) {
			var edge = Math.min(width, Math.min(geometry.width, geometry.height) * 0.5);
			canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, edge), color);
			canvas.fillRectIfPositive(new Rect(0.0, geometry.height - edge, geometry.width, edge), color);
			canvas.fillRectIfPositive(new Rect(0.0, edge, edge, geometry.height - edge * 2.0), color);
			canvas.fillRectIfPositive(new Rect(geometry.width - edge, edge, edge,
				geometry.height - edge * 2.0), color);
		}, "selection-field-frame");
		node.add(indicator);
	}

	public static function chevron(context:BuildContext, node:RenderNode, key:String,
			color:Color, open:Bool):Void {
		var indicator = overlay(context, key);
		indicator.onPaint(function(canvas, geometry) {
			var centerX = geometry.width - 16.0;
			var centerY = geometry.height * 0.5;
			var path = open
				? new PathBuilder().moveTo(centerX - 4.0, centerY + 2.0)
					.lineTo(centerX, centerY - 2.0).lineTo(centerX + 4.0, centerY + 2.0).build()
				: new PathBuilder().moveTo(centerX - 4.0, centerY - 2.0)
					.lineTo(centerX, centerY + 2.0).lineTo(centerX + 4.0, centerY - 2.0).build();
			canvas.strokeTransient(path, color, 1.75, LineCap.Round, LineJoin.Round);
		}, "selection-chevron:" + open);
		node.add(indicator);
	}

	public static function check(context:BuildContext, node:RenderNode, key:String,
			color:Color):Void {
		var indicator = overlay(context, key);
		indicator.onPaint(function(canvas, geometry) {
			var centerY = geometry.height * 0.5;
			var path = new PathBuilder().moveTo(10.0, centerY)
				.lineTo(13.0, centerY + 3.0).lineTo(18.0, centerY - 3.0).build();
			canvas.strokeTransient(path, color, 1.75, LineCap.Round, LineJoin.Round);
		}, "selection-check");
		node.add(indicator);
	}

	static function overlay(context:BuildContext, key:String):RenderNode {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.grow();
		style.positioning = LayoutPositioning.Absolute;
		style.zIndex = 2;
		var result = new RenderNode(context.id(key), LayoutVisualKind.Custom, style);
		result.hitTestSelf = false;
		return result;
	}
}
