package nativekit.ui.semantics;

import Rect;

/** Surface-local semantic record ready for projection into NativeKit. */
class AccessibilitySnapshotNode {
	public final id:Int;
	public final parentId:Int;
	public final childIndex:Int;
	public final role:AccessibilityRole;
	public final states:Int;
	public final actions:Int;
	public final bounds:Rect;
	public final semantics:Semantics;

	public function new(id:Int, parentId:Int, childIndex:Int, role:AccessibilityRole,
			states:Int, actions:Int, bounds:Rect, semantics:Semantics) {
		this.id = id;
		this.parentId = parentId;
		this.childIndex = childIndex;
		this.role = role;
		this.states = states;
		this.actions = actions;
		this.bounds = bounds;
		this.semantics = semantics;
	}
}
