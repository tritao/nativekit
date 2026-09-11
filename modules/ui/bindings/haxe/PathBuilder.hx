import NativeKitUI;
import NativeKitUI.Nkui_path_verb;

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
		append(Nkui_path_verb.NKUI_PATH_MOVE_TO, [x, y]);
		return this;
	}

	public function lineTo(x:Float, y:Float):PathBuilder {
		append(Nkui_path_verb.NKUI_PATH_LINE_TO, [x, y]);
		return this;
	}

	public function quadraticTo(controlX:Float, controlY:Float, x:Float, y:Float):PathBuilder {
		append(Nkui_path_verb.NKUI_PATH_QUADRATIC_TO, [controlX, controlY, x, y]);
		return this;
	}

	public function cubicTo(control1X:Float, control1Y:Float, control2X:Float, control2Y:Float, x:Float, y:Float):PathBuilder {
		append(Nkui_path_verb.NKUI_PATH_BEZIER_TO, [control1X, control1Y, control2X, control2Y, x, y]);
		return this;
	}

	public function arcTo(tangent1X:Float, tangent1Y:Float, tangent2X:Float, tangent2Y:Float, radius:Float):PathBuilder {
		append(Nkui_path_verb.NKUI_PATH_ARC_TO, [tangent1X, tangent1Y, tangent2X, tangent2Y, radius]);
		return this;
	}

	public function close():PathBuilder {
		append(Nkui_path_verb.NKUI_PATH_CLOSE, []);
		return this;
	}

	public function build():Path {
		if (elements.length == 0)
			throw "Cannot build an empty path";
		var made = NativeKitUI.nkui_path_create(elements);
		UiResult.check(made.status, "path.build");
		return new Path(made.out_path);
	}

	function append(verb:Nkui_path_verb, values:Array<Float>):Void {
		var element = new nkui_path_element();
		element.set_verb(verb);
		for (index in 0...values.length)
			element.set_values(index, values[index]);
		elements.push(element);
	}
}
