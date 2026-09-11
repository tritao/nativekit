import NativeKitUI;
import NativeKitUI.NkuiPathVerb;

/** Mutable encoder for immutable Path geometry. */
class PathBuilder {
	var elements:Array<nkui_path_element>;

	public function new() {
		elements = [];
	}

	public function clear():PathBuilder {
		elements = [];
		return this;
	}

	public function moveTo(x:Float, y:Float):PathBuilder {
		append(NkuiPathVerb.MoveTo, [x, y]);
		return this;
	}

	public function lineTo(x:Float, y:Float):PathBuilder {
		append(NkuiPathVerb.LineTo, [x, y]);
		return this;
	}

	public function quadraticTo(controlX:Float, controlY:Float, x:Float, y:Float):PathBuilder {
		append(NkuiPathVerb.QuadraticTo, [controlX, controlY, x, y]);
		return this;
	}

	public function cubicTo(control1X:Float, control1Y:Float, control2X:Float, control2Y:Float, x:Float, y:Float):PathBuilder {
		append(NkuiPathVerb.BezierTo, [control1X, control1Y, control2X, control2Y, x, y]);
		return this;
	}

	public function arcTo(tangent1X:Float, tangent1Y:Float, tangent2X:Float, tangent2Y:Float, radius:Float):PathBuilder {
		append(NkuiPathVerb.ArcTo, [tangent1X, tangent1Y, tangent2X, tangent2Y, radius]);
		return this;
	}

	public function close():PathBuilder {
		append(NkuiPathVerb.Close, []);
		return this;
	}

	public function build():Path {
		if (elements.length == 0)
			throw "Cannot build an empty path";
		var made = NativeKitUI.nkui_path_create(elements);
		UiResult.check(made.status, "path.build");
		return new Path(made.out_path);
	}

	function append(verb:NkuiPathVerb, values:Array<Float>):Void {
		var element = new nkui_path_element();
		element.set_verb(verb);
		for (index in 0...values.length)
			element.set_values(index, values[index]);
		elements.push(element);
	}
}
