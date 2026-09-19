package nativekit.ui.widgets;

import Canvas;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import LineCap;
import LineJoin;
import Paint;
import Path;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CachePolicy;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/**
 * A reusable path-backed custom view. Hit testing intentionally uses the
 * resolved layout bounds; path-accurate hit testing can be added separately.
 */
class Shape implements View {
	public final key:String;
	public final path:Path;
	public final paint:Paint;
	public final style:LayoutStyle;
	public final label:Null<String>;
	public final hitTestSelf:Bool;
	public final filled:Bool;
	public final strokeWidth:Float;
	public final strokeCap:LineCap;
	public final strokeJoin:LineJoin;
	public final miterLimit:Float;
	public final cachePolicy:CachePolicy;
	public final cacheKey:Null<String>;

	public function new(key:String, path:Path, paint:Paint, ?style:LayoutStyle, ?label:String,
			hitTestSelf:Bool = true, filled:Bool = true, strokeWidth:Float = 1.0,
			strokeCap:LineCap = LineCap.Butt, strokeJoin:LineJoin = LineJoin.Miter,
			miterLimit:Float = 4.0, cachePolicy:CachePolicy = CachePolicy.None,
			?cacheKey:String) {
		if (key == null || key.length == 0 || path == null || paint == null ||
			path.isDisposed() || paint.isDisposed())
			throw "Shapes require a stable key and live path and paint resources";
		if (!filled && (!Math.isFinite(strokeWidth) || strokeWidth <= 0.0))
			throw "Shape stroke width must be positive and finite";
		if (!Math.isFinite(miterLimit) || miterLimit <= 0.0)
			throw "Shape miter limit must be positive and finite";
		if (cacheKey != null && cacheKey.length == 0)
			throw "Shape cache keys cannot be empty";
		this.key = key;
		this.path = path;
		this.paint = paint;
		this.style = style == null ? defaultStyle() : style.copy();
		this.label = label;
		this.hitTestSelf = hitTestSelf;
		this.filled = filled;
		this.strokeWidth = strokeWidth;
		this.strokeCap = strokeCap;
		this.strokeJoin = strokeJoin;
		this.miterLimit = miterLimit;
		this.cachePolicy = cachePolicy;
		this.cacheKey = cacheKey;
	}

	/** Creates a filled path shape. */
	public static function fill(key:String, path:Path, paint:Paint, ?style:LayoutStyle,
			?label:String, hitTestSelf:Bool = true,
			cachePolicy:CachePolicy = CachePolicy.None, ?cacheKey:String):Shape {
		return new Shape(key, path, paint, style, label, hitTestSelf, true, 1.0,
			LineCap.Butt, LineJoin.Miter, 4.0, cachePolicy, cacheKey);
	}

	/** Creates a stroked path shape. */
	public static function stroke(key:String, path:Path, paint:Paint, width:Float,
			?style:LayoutStyle, ?label:String, hitTestSelf:Bool = true,
			cap:LineCap = LineCap.Butt, join:LineJoin = LineJoin.Miter,
			miterLimit:Float = 4.0, cachePolicy:CachePolicy = CachePolicy.None,
			?cacheKey:String):Shape {
		return new Shape(key, path, paint, style, label, hitTestSelf, false, width,
			cap, join, miterLimit, cachePolicy, cacheKey);
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("shape");
			var computed = context.resolveStyle(new StyleTarget("shape", key, key,
				null, ["shape"], context.interactionStates.get(nodeId)), style);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("shape", key, key, null, ["shape"]);
			node.states = context.interactionStates.get(nodeId);
			node.computedStyle = computed;
			node.hitTestSelf = hitTestSelf;
			node.cachePolicy = cachePolicy;
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Group, label);
			node.onPaint(function(canvas, _) {
				if (path.isDisposed() || paint.isDisposed())
					return;
				if (filled)
					canvas.fill(path, paint);
				else
					canvas.stroke(path, paint, strokeWidth, strokeCap, strokeJoin, miterLimit);
			}, cacheKey);
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(160.0);
		result.height = LayoutAxis.fixed(100.0);
		return result;
	}
}
