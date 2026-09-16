package components;

import GraphicsSurface;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import ShowcaseCube;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Retained native 3D surface composited as an ordinary UI view. */
class CubeView implements View {
	public final key:String;
	public final style:LayoutStyle;
	public final rotation:Float;
	public final label:String;

	public function new(key:String, rotation:Float, label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || label == null || label.length == 0 ||
			!Math.isFinite(rotation))
			throw "Cube views require a stable key, finite rotation, and accessible label";
		this.key = key;
		this.rotation = rotation;
		this.label = label;
		this.style = style == null ? defaultStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var id = context.id("cube-view");
			var surfaceState:State<GraphicsSurface> = context.resourceState(id,
				function() return ShowcaseCube.create(),
				function(value:GraphicsSurface) { value.dispose(); });
			var surface = surfaceState.value;
			ShowcaseCube.setRotation(surface, rotation);
			var node = new RenderNode(id, LayoutVisualKind.Custom, style);
			node.semantics = new Semantics(AccessibilityRole.Image, label);
			node.onPaint(function(canvas, geometry) {
				if (geometry.width > 0.0 && geometry.height > 0.0)
					canvas.drawSurface(surface, new Rect(0.0, 0.0, geometry.width, geometry.height));
			});
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(170.0);
		result.clipToParent = true;
		return result;
	}
}
