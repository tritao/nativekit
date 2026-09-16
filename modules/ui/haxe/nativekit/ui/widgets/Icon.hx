package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import LineCap;
import LineJoin;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.icons.IconData;
import nativekit.ui.icons.IconName;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Font-independent vector icon rendered from an embedded typed path. */
class Icon implements View {
	public final key:String;
	public final name:IconName;
	public final style:LayoutStyle;
	public var color:Null<Color>;
	public var label:Null<String>;

	public function new(key:String, name:IconName, size:Float = 16.0,
			?color:Color, ?label:String) {
		if (key == null || key.length == 0 || size <= 0.0 || !Math.isFinite(size))
			throw "Icons require a stable key and positive finite size";
		this.key = key;
		this.name = name;
		this.color = color;
		this.label = label;
		style = new LayoutStyle();
		style.width = LayoutAxis.fixed(size);
		style.height = LayoutAxis.fixed(size);
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("icon"), LayoutVisualKind.Custom, style);
			node.hitTestSelf = false;
			var paintColor = color == null ? context.theme.text : color;
			node.onPaint(function(canvas, geometry:ResolvedLayoutItem) {
				if (geometry.width <= 0.0 || geometry.height <= 0.0)
					return;
				canvas.withState(function(target) {
					target.scale(geometry.width / 24.0, geometry.height / 24.0);
					target.strokeTransient(IconData.build(name), paintColor, 2.0,
						LineCap.Round, LineJoin.Round);
				});
			});
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Image, label);
			return node;
		});
	}
}
