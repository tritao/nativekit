package nativekit.ui.core;

/** Pure transformations and validation for dock layout trees. */
class DockNodeTools {
	public static function clone(node:DockNode):DockNode {
		if (node == null)
			return DockNode.Empty;
		return switch (node) {
			case Empty: Empty;
			case Panel(id): Panel(id);
			case Tabs(ids, active): Tabs(ids == null ? [] : ids.copy(), active);
			case Split(axis, ratio, first, second):
				Split(axis, ratio, clone(first), clone(second));
		};
	}

	/** Removes redundant empty/single-child containers and clamps ratios. */
	public static function normalize(node:DockNode):DockNode {
		if (node == null)
			return DockNode.Empty;
		return switch (node) {
			case Empty: Empty;
			case Panel(id): id == null || id.length == 0 ? Empty : Panel(id);
			case Tabs(ids, active):
				var filtered:Array<String> = [];
				if (ids != null)
					for (id in ids)
						if (id != null && id.length > 0 && !containsId(filtered, id))
							filtered.push(id);
				if (filtered.length == 0)
					Empty;
				else if (filtered.length == 1)
					Panel(filtered[0]);
				else {
					var selected = containsId(filtered, active) ? active : filtered[0];
					Tabs(filtered, selected);
				}
			case Split(axis, ratio, first, second):
				var normalizedFirst = normalize(first);
				var normalizedSecond = normalize(second);
				switch ([normalizedFirst, normalizedSecond]) {
					case [Empty, Empty]: Empty;
					case [Empty, other]: other;
					case [other, Empty]: other;
					default: Split(axis, clampRatio(ratio), normalizedFirst, normalizedSecond);
				}
		};
	}

	/**
	 * Removes panels that are no longer registered by an application and then
	 * normalizes the remaining tree. This is intended for persisted layouts;
	 * strict programmatic layouts should continue to use validate().
	 */
	public static function keepKnown(node:DockNode, known:Map<String, Bool>):DockNode {
		if (node == null)
			return DockNode.Empty;
		return normalize(switch (node) {
			case Empty: Empty;
			case Panel(id): known != null && known.exists(id) ? Panel(id) : Empty;
			case Tabs(ids, active):
				var kept:Array<String> = [];
				if (ids != null)
					for (id in ids)
						if (known != null && known.exists(id) && !containsId(kept, id))
							kept.push(id);
				Tabs(kept, containsId(kept, active) ? active : (kept.length == 0 ? null : kept[0]));
			case Split(axis, ratio, first, second):
				Split(axis, ratio, keepKnown(first, known), keepKnown(second, known));
		});
	}

	public static function contains(node:DockNode, panelId:String):Bool {
		if (node == null || panelId == null)
			return false;
		return switch (node) {
			case Empty: false;
			case Panel(id): id == panelId;
			case Tabs(ids, _): containsId(ids, panelId);
			case Split(_, _, first, second): contains(first, panelId) || contains(second, panelId);
		};
	}

	public static function firstPanel(node:DockNode):Null<String> {
		if (node == null)
			return null;
		return switch (node) {
			case Empty: null;
			case Panel(id): id;
			case Tabs(ids, _): ids == null || ids.length == 0 ? null : ids[0];
			case Split(_, _, first, second):
				var leading = firstPanel(first);
				leading == null ? firstPanel(second) : leading;
		};
	}

	public static function panelIds(node:DockNode):Array<String> {
		var result:Array<String> = [];
		appendPanelIds(node, result);
		return result;
	}

	public static function same(first:DockNode, second:DockNode):Bool {
		if (first == null || second == null)
			return first == second;
		return switch ([first, second]) {
			case [Empty, Empty]: true;
			case [Panel(firstId), Panel(secondId)]: firstId == secondId;
			case [Tabs(firstIds, firstActive), Tabs(secondIds, secondActive)]:
				firstActive == secondActive && sameIds(firstIds, secondIds);
			case [Split(firstAxis, firstRatio, firstLeft, firstRight),
				Split(secondAxis, secondRatio, secondLeft, secondRight)]:
				firstAxis == secondAxis && firstRatio == secondRatio && same(firstLeft, secondLeft) &&
					same(firstRight, secondRight);
			default: false;
		};
	}

	public static function activate(node:DockNode, panelId:String):DockNode {
		if (node == null || panelId == null)
			return DockNode.Empty;
		return switch (node) {
			case Empty: Empty;
			case Panel(id): Panel(id);
			case Tabs(ids, active):
				Tabs(ids == null ? [] : ids.copy(), containsId(ids, panelId) ? panelId : active);
			case Split(axis, ratio, first, second):
				Split(axis, ratio, activate(first, panelId), activate(second, panelId));
		};
	}

	public static function remove(node:DockNode, panelId:String):DockNode {
		if (node == null || panelId == null)
			return DockNode.Empty;
		return normalize(switch (node) {
			case Empty: Empty;
			case Panel(id): id == panelId ? Empty : Panel(id);
			case Tabs(ids, active):
				var remaining:Array<String> = [];
				if (ids != null)
					for (id in ids)
						if (id != panelId)
							remaining.push(id);
				Tabs(remaining, active == panelId && remaining.length > 0 ? remaining[0] : active);
			case Split(axis, ratio, first, second):
				Split(axis, ratio, remove(first, panelId), remove(second, panelId));
		});
	}

	/** Moves or inserts a panel relative to the target panel. */
	public static function dock(node:DockNode, panelId:String, targetPanelId:String,
		zone:DockDropZone):DockNode {
		if (node == null || panelId == null || targetPanelId == null || panelId == targetPanelId ||
			!contains(node, targetPanelId))
			return normalize(node);
		var without = contains(node, panelId) ? remove(node, panelId) : clone(node);
		return normalize(insert(without, panelId, targetPanelId, zone));
	}

	/** Updates a split addressed by a child-index path from the root. */
	public static function setSplitRatio(node:DockNode, path:Array<Int>, ratio:Float,
		depth:Int = 0):DockNode {
		if (node == null || path == null || depth < 0)
			return normalize(node);
		if (depth == path.length)
			return switch (node) {
				case Split(axis, _, first, second): Split(axis, clampRatio(ratio), first, second);
				default: node;
			};
		return switch (node) {
			case Split(axis, current, first, second):
				var branch = path[depth];
				branch == 0 ? Split(axis, current, setSplitRatio(first, path, ratio, depth + 1), second) :
					branch == 1 ? Split(axis, current, first, setSplitRatio(second, path, ratio, depth + 1)) : node;
			default: node;
		};
	}

	/** Validates IDs, uniqueness, active tabs, and split ratios. */
	public static function validate(node:DockNode, known:Map<String, Bool>):Null<String> {
		var seen:Map<String, Bool> = new Map();
		return validateNode(node, known, seen);
	}

	static function validateNode(node:DockNode, known:Map<String, Bool>,
		seen:Map<String, Bool>):Null<String> {
		if (node == null)
			return "Dock layout contains a null node";
		return switch (node) {
			case Empty: null;
			case Panel(id): validatePanel(id, known, seen);
			case Tabs(ids, active):
				if (ids == null || ids.length == 0)
					"Dock tab groups cannot be empty";
				else if (!containsId(ids, active))
					"Dock tab groups require an active panel";
				else {
					var error:Null<String> = null;
					for (id in ids)
						if (error == null)
							error = validatePanel(id, known, seen);
					error;
				}
			case Split(_, ratio, first, second):
				if (!finite(ratio) || ratio <= 0.0 || ratio >= 1.0)
					"Dock split ratios must be between zero and one";
				else {
					var firstError = validateNode(first, known, seen);
					firstError == null ? validateNode(second, known, seen) : firstError;
				}
		};
	}

	static function validatePanel(id:String, known:Map<String, Bool>,
		seen:Map<String, Bool>):Null<String> {
		if (id == null || id.length == 0)
			return "Dock panels require stable IDs";
		if (known != null && !known.exists(id))
			return "Dock layout references an unregistered panel: " + id;
		if (seen.exists(id))
			return "Dock layout references a panel more than once: " + id;
		seen.set(id, true);
		return null;
	}

	static function insert(node:DockNode, panelId:String, targetPanelId:String,
		zone:DockDropZone):DockNode {
		return switch (node) {
			case Empty: Empty;
			case Panel(id):
				if (id != targetPanelId)
					Panel(id);
				else
					switch (zone) {
						case Center: Tabs([id, panelId], panelId);
						case TabBefore: Tabs([panelId, id], panelId);
						case TabAfter: Tabs([id, panelId], panelId);
						case Left: Split(DockSplitAxis.Horizontal, 0.3, Panel(panelId), Panel(id));
						case Right: Split(DockSplitAxis.Horizontal, 0.7, Panel(id), Panel(panelId));
						case Top: Split(DockSplitAxis.Vertical, 0.3, Panel(panelId), Panel(id));
						case Bottom: Split(DockSplitAxis.Vertical, 0.7, Panel(id), Panel(panelId));
					};
			case Tabs(ids, active):
				if (!containsId(ids, targetPanelId))
					Tabs(ids == null ? [] : ids.copy(), active);
				else
					switch (zone) {
						case Center:
							var next:Array<String> = ids == null ? [] : ids.copy();
							if (!containsId(next, panelId))
								next.push(panelId);
							Tabs(next, panelId);
						case TabBefore | TabAfter:
							insertTab(ids, panelId, targetPanelId, zone);
						case Left | Right | Top | Bottom:
							var group = Tabs(ids == null ? [] : ids.copy(), active);
							splitAround(group, panelId, zone);
					};
			case Split(axis, ratio, first, second):
				if (contains(first, targetPanelId))
					Split(axis, ratio, insert(first, panelId, targetPanelId, zone), second);
				else if (contains(second, targetPanelId))
					Split(axis, ratio, first, insert(second, panelId, targetPanelId, zone));
				else
					Split(axis, ratio, first, second);
		};
	}

	static function splitAround(target:DockNode, panelId:String, zone:DockDropZone):DockNode {
		return switch (zone) {
			case Left: Split(DockSplitAxis.Horizontal, 0.3, Panel(panelId), target);
			case Right: Split(DockSplitAxis.Horizontal, 0.7, target, Panel(panelId));
			case Top: Split(DockSplitAxis.Vertical, 0.3, Panel(panelId), target);
			case Bottom: Split(DockSplitAxis.Vertical, 0.7, target, Panel(panelId));
			case Center | TabBefore | TabAfter: target;
		};
	}

	static function insertTab(ids:Array<String>, panelId:String, targetPanelId:String,
			zone:DockDropZone):DockNode {
		var next:Array<String> = [];
		var inserted = false;
		if (ids != null)
			for (id in ids) {
				if (id == panelId)
					continue;
				if (!inserted && id == targetPanelId && zone == DockDropZone.TabBefore) {
					next.push(panelId);
					inserted = true;
				}
				next.push(id);
				if (!inserted && id == targetPanelId && zone == DockDropZone.TabAfter) {
					next.push(panelId);
					inserted = true;
				}
			}
		if (!inserted)
			next.push(panelId);
		return Tabs(next, panelId);
	}

	static function appendPanelIds(node:DockNode, result:Array<String>):Void {
		switch (node) {
			case Empty:
			case Panel(id):
				if (id != null && !containsId(result, id))
					result.push(id);
			case Tabs(ids, _):
				if (ids != null)
					for (id in ids)
						if (!containsId(result, id))
							result.push(id);
			case Split(_, _, first, second):
				appendPanelIds(first, result);
				appendPanelIds(second, result);
		}
	}

	static function containsId(ids:Array<String>, id:String):Bool {
		if (ids == null || id == null)
			return false;
		for (item in ids)
			if (item == id)
				return true;
		return false;
	}

	static function sameIds(first:Array<String>, second:Array<String>):Bool {
		if (first == null || second == null)
			return first == second;
		if (first.length != second.length)
			return false;
		for (index in 0...first.length)
			if (first[index] != second[index])
				return false;
		return true;
	}

	static inline function clampRatio(value:Float):Float {
		if (!finite(value))
			return 0.5;
		return value < 0.05 ? 0.05 : value > 0.95 ? 0.95 : value;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
