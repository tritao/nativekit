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
	public final setSize:Int;
	public final positionInSet:Int;
	public final rowCount:Int;
	public final columnCount:Int;
	public final rowIndex:Int;
	public final columnIndex:Int;
	public final rowSpan:Int;
	public final columnSpan:Int;
	public final hierarchyLevel:Int;
	public final orientation:AccessibilityOrientation;

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
		setSize = semantics.setSize;
		positionInSet = semantics.positionInSet;
		rowCount = semantics.rowCount;
		columnCount = semantics.columnCount;
		rowIndex = semantics.rowIndex;
		columnIndex = semantics.columnIndex;
		rowSpan = semantics.rowSpan;
		columnSpan = semantics.columnSpan;
		hierarchyLevel = semantics.hierarchyLevel;
		orientation = semantics.orientation;
	}
}
