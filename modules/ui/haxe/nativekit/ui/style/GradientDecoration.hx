package nativekit.ui.style;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;

/** Horizontal linear gradient backed by the renderer's native gradient paint. */
class GradientDecoration extends Decoration {
	public final start:Color;
	public final end:Color;

	public function new(start:Color, end:Color) {
		super(DecorationKind.Gradient);
		if (start == null || end == null)
			throw "Gradients require colors";
		this.start = Effect.copyColor(start);
		this.end = Effect.copyColor(end);
	}

	override public function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void {
		canvas.fillLinearGradientRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
			0.0, 0.0, geometry.width, 0.0,
			[new GradientStop(0.0, start), new GradientStop(1.0, end)]);
	}

	override public function copy():Decoration
		return new GradientDecoration(start, end);

	override public function isEqual(other:Decoration):Bool {
		if (other == null || other.kind != kind)
			return false;
		var value:GradientDecoration = cast other;
		return Effect.equalColor(start, value.start) && Effect.equalColor(end, value.end);
	}

	override public function interpolate(other:Decoration, amount:Float):Decoration {
		if (other == null || other.kind != kind)
			return Decoration.discrete(this, other, amount);
		var value:GradientDecoration = cast other;
		return new GradientDecoration(Effect.interpolateColor(start, value.start, amount),
			Effect.interpolateColor(end, value.end, amount));
	}

	override public function describe():String
		return 'linear-gradient(${start.red},${start.green},${start.blue},${start.alpha},' +
			'${end.red},${end.green},${end.blue},${end.alpha})';
}
