package nativekit.ui.widgets;

import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;

/**
 * Contributes a command scope to the focused subtree without changing the
 * registry's global scope stack. Focused descendants automatically receive
 * this scope's shortcut precedence during keyboard routing.
 */
class CommandScope implements View {
	public final scope:String;
	final child:View;

	public function new(scope:String, child:View) {
		if (scope == null || scope.length == 0 || child == null)
			throw "Command scopes require a stable name and child view";
		this.scope = scope;
		this.child = child;
	}

	public function build(context:BuildContext):RenderNode {
		var node = child.build(context);
		node.commandScope = scope;
		return node;
	}
}
