package nativekit.ui.widgets;

import Canvas;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/** Custom Haxe-painted region with ordinary Haxe-routed input handlers. */
class CanvasView implements View {
	public final key:String;
	public final label:Null<String>;
	public final style:LayoutStyle;
	public final hitTestSelf:Bool;
	final painter:Canvas->ResolvedLayoutItem->Void;
	final handlers:Map<String, Array<UiEvent->Void>>;

	public function new(key:String, painter:Canvas->ResolvedLayoutItem->Void,
			?style:LayoutStyle, ?label:String, hitTestSelf:Bool = true) {
		if (key == null || key.length == 0 || painter == null)
			throw "Canvas views require a stable key and painter";
		this.key = key;
		this.label = label;
		this.hitTestSelf = hitTestSelf;
		this.painter = painter;
		this.style = style == null ? defaultStyle() : style.copy();
		handlers = new Map();
	}

	public function on(kind:String, handler:UiEvent->Void):CanvasView {
		if (kind == null || kind.length == 0 || handler == null)
			throw "Canvas view handlers require an event kind and callback";
		var values = handlers.get(kind);
		if (values == null) {
			values = [];
			handlers.set(kind, values);
		}
		values.push(handler);
		return this;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("canvas");
			var computed = context.resolveStyle(new StyleTarget("canvas", key, key,
				null, ["canvas"], context.interactionStates.get(nodeId)), style);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("canvas", key, key, null, ["canvas"]);
			node.states = context.interactionStates.get(nodeId);
			node.computedStyle = computed;
			node.hitTestSelf = hitTestSelf;
			if (label != null)
				node.semantics = new Semantics(AccessibilityRole.Group, label);
			node.onPaint(painter);
			for (kind in handlers.keys())
				for (handler in handlers.get(kind))
					node.on(kind, handler);
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
