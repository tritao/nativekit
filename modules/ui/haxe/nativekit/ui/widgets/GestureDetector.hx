package nativekit.ui.widgets;

import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.gestures.GestureArena;
import nativekit.ui.gestures.GestureRecognizer;

/** Attaches Haxe tap, double-tap, long-press, and drag recognizers to a child view. */
class GestureDetector implements View {
	final key:Key;
	final child:View;
	final recognizers:Array<GestureRecognizer>;
	public final style:LayoutStyle;

	public function new(key:String, child:View, recognizers:Array<GestureRecognizer>,
			?style:LayoutStyle) {
		if (child == null)
			throw "GestureDetector requires a child view";
		this.key = new Key(key);
		this.child = child;
		this.recognizers = recognizers == null ? [] : recognizers.copy();
		this.style = style == null ? new LayoutStyle() : style.copy();
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var node = new RenderNode(context.id("gesture-detector"), LayoutVisualKind.Box, style);
			node.hitTestSelf = false;
			var content = context.withScope(new Key("content"), function() return child.build(context));
			node.add(content);
			var arena = context.gestures;
			node.on(UiEventKind.PointerDown, function(event) {
				if (event.button == 0)
					arena.pointerDown(content.id, event, recognizers);
			});
			node.on(UiEventKind.PointerMove, function(event) { arena.pointerMove(event); });
			node.on(UiEventKind.PointerUp, function(event) { arena.pointerUp(event); });
			node.on(UiEventKind.PointerCancel, function(event) { arena.pointerCancel(event.pointerId); });
			return node;
		});
	}
}
