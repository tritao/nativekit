package nativekit.ui.semantics;

import NativeKit;
import NativeKitSurface;
import NativeKitResult;
import Rect;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.WidgetId;

/** Projects Haxe semantics and resolved geometry into NativeKit's virtual tree. */
class AccessibilityBridge {
	final surface:NativeKitSurface;
	var previousParents:Map<Int, Int>;
	var focusedId:Int;
	var disposed:Bool;

	public function new(surface:NativeKitSurface) {
		if (surface == null || surface.isDisposed())
			throw "Accessibility projection requires a live NativeKit surface";
		this.surface = surface;
		NativeKitResult.check(NativeKit.nk_surface_accessibility_clear(surface.nativeHandle()),
			"ui.accessibility.initialClear");
		previousParents = new Map();
		focusedId = NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT;
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

	/** Reconciles semantic nodes and removes platform nodes absent from this frame. */
	public function update(root:Null<RenderNode>, focused:Null<WidgetId>):Void {
		ensureLive();
		var snapshot = project(root, focused);
		var currentParents:Map<Int, Int> = new Map();
		for (item in snapshot)
			currentParents.set(item.id, item.parentId);

		for (id in previousParents.keys()) {
			if (!currentParents.exists(id)) {
				var parentId = previousParents.get(id);
				if (parentId == NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT ||
					currentParents.exists(parentId))
					NativeKitResult.check(NativeKit.nk_surface_accessibility_remove_node(
						surface.nativeHandle(), id), "ui.accessibility.removeNode");
			}
		}

		for (item in snapshot)
			setNode(item);
		var nextFocus = NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT;
		for (item in snapshot)
			if (focused != null && item.id == focused.value) {
				nextFocus = item.id;
				break;
			}
		if (nextFocus != focusedId) {
			NativeKitResult.check(NativeKit.nk_surface_accessibility_set_focus(
				surface.nativeHandle(), nextFocus), "ui.accessibility.setFocus");
			focusedId = nextFocus;
		}
		previousParents = currentParents;
	}

	/** Removes the projected tree if the host surface still exists. */
	public function dispose():Void {
		if (disposed)
			return;
		if (!surface.isDisposed())
			NativeKitResult.check(NativeKit.nk_surface_accessibility_clear(surface.nativeHandle()),
				"ui.accessibility.clear");
		disposed = true;
		previousParents = new Map();
	}

	function setNode(item:AccessibilitySnapshotNode):Void {
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
		NativeKitResult.check(NativeKit.nk_surface_accessibility_set_node(surface.nativeHandle(), node),
			"ui.accessibility.setNode");
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
		var enabled = ancestorsEnabled && node.enabled;
		var nextParent = semanticParent;
		if (node.semantics != null) {
			var semantics:Semantics = cast node.semantics;
			var childIndex = childCounts.exists(semanticParent) ? childCounts.get(semanticParent) : 0;
			childCounts.set(semanticParent, childIndex + 1);
			var states = semantics.states;
			if (node.focusable)
				states |= AccessibilityState.Focusable;
			if (!enabled)
				states |= AccessibilityState.Disabled;
			if (focused != null && node.id.equals(focused))
				states |= AccessibilityState.Focused;
			var actions = semantics.actions;
			if (node.focusable && enabled)
				actions |= AccessibilityAction.Focus;
			if (!enabled)
				actions = 0;
			output.push(new AccessibilitySnapshotNode(node.id.value, semanticParent,
				childIndex, semantics.role, states, actions, visibleBounds(node.resolved), semantics));
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

	static function visibleBounds(item:ResolvedLayoutItem):Rect {
		var transform = item.transform;
		var x0 = transform.a * item.x + transform.c * item.y + transform.tx;
		var y0 = transform.b * item.x + transform.d * item.y + transform.ty;
		var x1 = transform.a * (item.x + item.width) + transform.c * item.y + transform.tx;
		var y1 = transform.b * (item.x + item.width) + transform.d * item.y + transform.ty;
		var x2 = transform.a * item.x + transform.c * (item.y + item.height) + transform.tx;
		var y2 = transform.b * item.x + transform.d * (item.y + item.height) + transform.ty;
		var x3 = transform.a * (item.x + item.width) + transform.c * (item.y + item.height) + transform.tx;
		var y3 = transform.b * (item.x + item.width) + transform.d * (item.y + item.height) + transform.ty;
		var left = Math.max(item.clipBounds.x, Math.min(Math.min(x0, x1), Math.min(x2, x3)));
		var top = Math.max(item.clipBounds.y, Math.min(Math.min(y0, y1), Math.min(y2, y3)));
		var right = Math.min(item.clipBounds.x + item.clipBounds.width,
			Math.max(Math.max(x0, x1), Math.max(x2, x3)));
		var bottom = Math.min(item.clipBounds.y + item.clipBounds.height,
			Math.max(Math.max(y0, y1), Math.max(y2, y3)));
		return new Rect(left, top, Math.max(0.0, right - left), Math.max(0.0, bottom - top));
	}
}
