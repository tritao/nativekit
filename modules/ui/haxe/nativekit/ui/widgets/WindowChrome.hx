package nativekit.ui.widgets;

import NativeKit.WindowDecorationRegionKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.CursorShape;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/**
	Annotates a view with native window-chrome behavior after it has been built.
	Child regions are emitted after their parent, allowing client controls to
	carve interactive holes out of a larger drag region.
*/
class WindowChrome implements View {
	final key:nativekit.ui.core.Key;
	final child:View;
	final kind:WindowDecorationRegionKind;
	final cursor:Null<CursorShape>;

	public function new(key:String, kind:WindowDecorationRegionKind, child:View,
			?cursor:CursorShape) {
		if (key == null || key.length == 0 || kind == null || child == null)
			throw "Window chrome views require a key, kind, and child";
		this.key = new nativekit.ui.core.Key(key);
		this.kind = kind;
		this.child = child;
		this.cursor = cursor;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var node = child.build(context);
			if (node == null || node.parent != null)
				throw "A window chrome child must produce one unparented render root";
			return node.setWindowDecoration(kind, cursor);
		});
	}
}
