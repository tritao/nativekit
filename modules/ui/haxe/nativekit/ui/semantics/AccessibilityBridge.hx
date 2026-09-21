package nativekit.ui.semantics;

import NativeKit;
import NativeKitSurface;
import Rect;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.WidgetId;

/** Projects Haxe semantics and resolved geometry into NativeKit's virtual tree. */
class AccessibilityBridge {
	final surface:NativeKitSurface;
	var previousParents:Map<Int, Int>;
	var disposed:Bool;

	public function new(surface:NativeKitSurface) {
		if (surface == null || surface.isDisposed())
			throw "Accessibility projection requires a live NativeKit surface";
		this.surface = surface;
		NativeKit.nk_surface_accessibility_clear_checked(surface.nativeHandle());
		previousParents = new Map();
		disposed = false;
	}

	/** Builds a stable, parent-first snapshot for the current resolved tree. */
	public static function project(root:Null<RenderNode>, focused:Null<WidgetId>):Array<AccessibilitySnapshotNode> {
		var result:Array<AccessibilitySnapshotNode> = [];
		var childCounts:Map<Int, Int> = new Map();
		if (root == null)
			return result;
		var semanticRoot = findFocusTrap(root);
		if (semanticRoot == null)
			semanticRoot = root;
		projectNode(semanticRoot, NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT,
			true, focused, result, childCounts);
		return result;
	}

	/** Sends one atomic snapshot replacement, removals, and focus update. */
	public function update(root:Null<RenderNode>, focused:Null<WidgetId>):Void {
		ensureLive();
		var snapshot = project(root, focused);
		var currentParents:Map<Int, Int> = new Map();
		for (item in snapshot)
			currentParents.set(item.id, item.parentId);
		var requestedFocus = focused == null ? NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT : focused.value;
		var batch = buildUpdate(snapshot, previousParents, requestedFocus);
		NativeKit.nk_surface_accessibility_update_with_removed_ids_checked(surface.nativeHandle(),
			batch.nativeUpdate, batch.removedNodeIds, batch.removedNodeIds.length);
		previousParents = currentParents;
	}

	/** Builds a platform-neutral atomic NativeKit update from a complete semantic snapshot. */
	public static function buildUpdate(snapshot:Array<AccessibilitySnapshotNode>,
			previousParents:Map<Int, Int>, focus:Int):AccessibilityUpdateBatch {
		var currentIds:Map<Int, Bool> = new Map();
		for (item in snapshot)
			currentIds.set(item.id, true);
		var nodes = buildNodes(snapshot);
		var removed:Array<Int> = [];
		for (id in previousParents.keys()) {
			if (!currentIds.exists(id)) {
				var parentId = previousParents.get(id);
				if (parentId == NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT ||
					currentIds.exists(parentId))
					removed.push(id);
			}
		}

		var update = new NativeKit.AccessibilityUpdate();
		update.set_struct_size(NativeKit.AccessibilityUpdate.size());
		update.set_flags(NativeKit.AccessibilityUpdateFlags.NkAccessibilityUpdateFocus);
		update.set_nodes(nodes);
		update.set_node_count(nodes.length);
		var removedBytes = haxe.io.Bytes.alloc(removed.length * 4);
		for (index in 0...removed.length) {
			var offset = index * 4;
			var id = removed[index];
			removedBytes.set(offset, id & 0xff);
			removedBytes.set(offset + 1, (id >>> 8) & 0xff);
			removedBytes.set(offset + 2, (id >>> 16) & 0xff);
			removedBytes.set(offset + 3, (id >>> 24) & 0xff);
		}
		update.set_removed_node_count(0);
		var focusTarget = NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT;
		for (item in snapshot)
			if (item.id == focus && (item.states & AccessibilityState.Disabled) == 0) {
				focusTarget = item.id;
				break;
			}
		update.set_focus(focusTarget);
		update.set_reserved(0);
		return new AccessibilityUpdateBatch(update, removedBytes, removed.length);
	}

	/** Serializes every record in a semantic snapshot into native ABI structures. */
	public static function buildNodes(snapshot:Array<AccessibilitySnapshotNode>):Array<NativeKit.AccessibilityNode> {
		var result:Array<NativeKit.AccessibilityNode> = [];
		for (item in snapshot)
			result.push(toNativeNode(item));
		return result;
	}

	/** Removes the projected tree if the host surface still exists. */
	public function dispose():Void {
		if (disposed)
			return;
		if (!surface.isDisposed())
			NativeKit.nk_surface_accessibility_clear_checked(surface.nativeHandle());
		disposed = true;
		previousParents = new Map();
	}

	static function toNativeNode(item:AccessibilitySnapshotNode):NativeKit.AccessibilityNode {
		var value = item.semantics;
		var node = new NativeKit.AccessibilityNode();
		node.set_struct_size(NativeKit.AccessibilityNode.size());
		node.set_id(item.id);
		node.set_parent_id(item.parentId);
		node.set_child_index(item.childIndex);
		node.set_role(cast(item.role, NativeKit.AccessibilityRole));
		node.set_states(cast(item.states, NativeKit.AccessibilityStates));
		node.set_actions(cast(item.actions, NativeKit.AccessibilityActions));
		node.set_reserved0(0);
		node.set_x(item.bounds.x);
		node.set_y(item.bounds.y);
		node.set_width(item.bounds.width);
		node.set_height(item.bounds.height);
		node.set_label(value.label);
		node.set_value(value.value);
		node.set_numeric_value(value.numericValue);
		node.set_numeric_minimum(value.numericMinimum);
		node.set_numeric_maximum(value.numericMaximum);
		node.set_text_start(value.textStart);
		node.set_document_length(value.documentLength);
		node.set_selection_start(value.selectionStart);
		node.set_selection_end(value.selectionEnd);
		node.set_set_size(item.setSize);
		node.set_position_in_set(item.positionInSet);
		node.set_row_count(item.rowCount);
		node.set_column_count(item.columnCount);
		node.set_row_index(item.rowIndex);
		node.set_column_index(item.columnIndex);
		node.set_row_span(item.rowSpan);
		node.set_column_span(item.columnSpan);
		node.set_hierarchy_level(item.hierarchyLevel);
		node.set_orientation(cast(item.orientation, NativeKit.AccessibilityOrientation));
		return node;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Accessibility bridge has been disposed";
		if (surface.isDisposed())
			throw "Accessibility host surface has been disposed";
	}

	static function projectNode(node:RenderNode, semanticParent:Int, ancestorsEnabled:Bool,
			focused:Null<WidgetId>, output:Array<AccessibilitySnapshotNode>, childCounts:Map<Int, Int>):Void {
		if (node.resolved == null || !node.resolved.visible)
			return;
		var explicitlyDisabled = false;
		if (node.semantics != null) {
			var declared:Semantics = cast node.semantics;
			explicitlyDisabled = (declared.states & AccessibilityState.Disabled) != 0;
		}
		var enabled = ancestorsEnabled && node.enabled && !explicitlyDisabled;
		var nextParent = semanticParent;
		if (node.semantics != null) {
			var semantics:Semantics = cast node.semantics;
			var childIndex = childCounts.exists(semanticParent) ? childCounts.get(semanticParent) : 0;
			childCounts.set(semanticParent, childIndex + 1);
			var states = semantics.states;
			if (node.focusable && enabled)
				states |= AccessibilityState.Focusable;
			if (!enabled) {
				states |= AccessibilityState.Disabled;
				states &= ~(AccessibilityState.Focusable | AccessibilityState.Focused);
			}
			if (enabled && focused != null && node.id.equals(focused))
				states |= AccessibilityState.Focused;
			var actions = semantics.actions;
			if (node.focusable && enabled)
				actions |= AccessibilityAction.Focus;
			if (!enabled)
				actions = 0;
			output.push(new AccessibilitySnapshotNode(node.id.value, semanticParent,
				childIndex, semantics.role, states, actions,
				node.resolved.clippedViewportBounds(), semantics));
			nextParent = node.id.value;
		}
		for (child in node.children)
			projectNode(child, nextParent, enabled, focused, output, childCounts);
	}

	static function findFocusTrap(node:Null<RenderNode>):Null<RenderNode> {
		if (node == null || node.resolved == null || !node.resolved.visible)
			return null;
		var result:Null<RenderNode> = node.focusTrap ? node : null;
		for (child in node.children) {
			var nested = findFocusTrap(child);
			if (nested != null)
				result = nested;
		}
		return result;
	}

}
