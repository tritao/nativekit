package nativekit.ui.debug;

import nativekit.ui.core.RenderNode;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Small deterministic audit for common accessibility mistakes in resolved trees. */
class AccessibilityAudit {
	public static function inspect(root:Null<RenderNode>):Array<AccessibilityIssue> {
		var issues:Array<AccessibilityIssue> = [];
		if (root == null)
			return issues;
		var ids:Map<Int, Bool> = new Map();
		root.walk(function(node) {
			if (ids.exists(node.id.value))
				issues.push(new AccessibilityIssue(node.id.value, "duplicate-id",
					"Render node IDs must be unique within a frame"));
			ids.set(node.id.value, true);
			var semantics:Null<Semantics> = cast node.semantics;
			if ((node.focusable || semantics != null && semantics.actions != 0) && semantics == null)
				issues.push(new AccessibilityIssue(node.id.value, "missing-semantics",
					"Interactive nodes need an accessibility role and state"));
			if (semantics != null && needsName(semantics.role) &&
				(semantics.label == null || StringTools.trim(semantics.label).length == 0))
				issues.push(new AccessibilityIssue(node.id.value, "missing-label",
					"Interactive accessibility nodes need a non-empty label"));
			if (node.focusable && node.resolved != null && node.resolved.visible &&
				(node.resolved.width <= 0.0 || node.resolved.height <= 0.0))
				issues.push(new AccessibilityIssue(node.id.value, "empty-focus-bounds",
					"Visible focusable nodes need positive resolved bounds"));
		});
		return issues;
	}

	public static function isValid(root:Null<RenderNode>):Bool
		return inspect(root).length == 0;

	static function needsName(role:AccessibilityRole):Bool {
		return role == AccessibilityRole.Button || role == AccessibilityRole.Checkbox ||
			role == AccessibilityRole.Radio || role == AccessibilityRole.Link ||
			role == AccessibilityRole.Slider || role == AccessibilityRole.TextField;
	}
}
