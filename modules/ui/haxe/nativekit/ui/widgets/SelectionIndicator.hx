package nativekit.ui.widgets;

import Color;
import LineCap;
import LineJoin;
import PathBuilder;
import nativekit.ui.core.RenderNode;

/** Small font-independent affordances shared by selection controls. */
class SelectionIndicator {
	public static function chevron(node:RenderNode, color:Color, open:Bool):Void {
		node.onPaint(function(canvas, geometry) {
			var centerX = geometry.width - 16.0;
			var centerY = geometry.height * 0.5;
			var path = open
				? new PathBuilder().moveTo(centerX - 4.0, centerY + 2.0)
					.lineTo(centerX, centerY - 2.0).lineTo(centerX + 4.0, centerY + 2.0).build()
				: new PathBuilder().moveTo(centerX - 4.0, centerY - 2.0)
					.lineTo(centerX, centerY + 2.0).lineTo(centerX + 4.0, centerY - 2.0).build();
			canvas.strokeTransient(path, color, 1.75, LineCap.Round, LineJoin.Round);
		}, "selection-chevron:" + open);
	}

	public static function check(node:RenderNode, color:Color):Void {
		node.onPaint(function(canvas, geometry) {
			var centerY = geometry.height * 0.5;
			var path = new PathBuilder().moveTo(10.0, centerY)
				.lineTo(13.0, centerY + 3.0).lineTo(18.0, centerY - 3.0).build();
			canvas.strokeTransient(path, color, 1.75, LineCap.Round, LineJoin.Round);
		}, "selection-check");
	}
}
