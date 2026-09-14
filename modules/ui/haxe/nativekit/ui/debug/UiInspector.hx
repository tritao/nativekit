package nativekit.ui.debug;

import nativekit.ui.core.RenderNode;
import nativekit.ui.core.HitTest;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.Semantics;

/** Produces deterministic, headless snapshots and readable render-tree dumps. */
class UiInspector {
	public static function snapshot(root:Null<RenderNode>, focused:Null<WidgetId>,
			hovered:Null<WidgetId> = null, pressed:Null<WidgetId> = null):Array<UiNodeSnapshot> {
		var result:Array<UiNodeSnapshot> = [];
		if (root != null) {
			var hoveredOwner = interactionOwner(root, hovered);
			var pressedOwner = interactionOwner(root, pressed);
			append(root, 0, 0, focused, hoveredOwner, pressedOwner, result);
		}
		return result;
	}

	public static function dump(root:Null<RenderNode>, focused:Null<WidgetId>):String {
		var records = snapshot(root, focused);
		var lines:Array<String> = [];
		for (record in records) {
			var prefix = "";
			for (_ in 0...record.depth)
				prefix += "  ";
			var line = prefix + "#" + Std.string(record.id) + " " + visualName(record.visualKind) +
				" [" + number(record.bounds.x) + "," + number(record.bounds.y) + " " +
				number(record.bounds.width) + "x" + number(record.bounds.height) + "]";
			if (!record.visible)
				line += " hidden";
			if (record.focused)
				line += " focused";
			if (record.hovered)
				line += " hovered";
			if (record.pressed)
				line += " pressed";
			if (record.focusable)
				line += " focusable";
			if (record.zIndex != 0)
				line += " z=" + Std.string(record.zIndex);
			if (record.role >= 0)
				line += " role=" + Std.string(record.role) + " states=" + Std.string(record.semanticStates);
			if (record.label != null && record.label.length > 0)
				line += ' label="' + escape(record.label) + '"';
			if (record.value != null)
				line += ' value="' + escape(record.value) + '"';
			lines.push(line);
		}
		return lines.join("\n");
	}

	static function append(node:RenderNode, parentId:Int, depth:Int,
			focused:Null<WidgetId>, hovered:Null<WidgetId>, pressed:Null<WidgetId>,
			output:Array<UiNodeSnapshot>):Void {
		var semantics:Null<Semantics> = cast node.semantics;
		var geometry = node.resolved;
		var bounds = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.bounds();
		var clip = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.clipBounds;
		var content = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.contentBounds;
		var role = semantics == null ? -1 : cast semantics.role;
		output.push(new UiNodeSnapshot(node.id.value, parentId, depth,
			cast node.layout.visualKind, bounds, clip, content,
			geometry != null && geometry.visible, node.enabled, node.focusable,
			focused != null && focused.equals(node.id),
			hovered != null && hovered.equals(node.id),
			pressed != null && pressed.equals(node.id), node.layout.style.zIndex,
			role, semantics == null ? 0 : semantics.states,
			semantics == null ? null : semantics.label,
			semantics == null ? null : semantics.value,
			semantics == null ? 0 : semantics.actions));
		for (child in node.children)
			append(child, node.id.value, depth + 1, focused, hovered, pressed, output);
	}

	static function interactionOwner(root:RenderNode, id:Null<WidgetId>):Null<WidgetId> {
		if (id == null)
			return null;
		var path = HitTest.pathTo(root.find(id));
		var semantic:Null<WidgetId> = null;
		var index = path.length - 1;
		while (index >= 0) {
			var node = path[index];
			if (node.semantics != null && semantic == null)
				semantic = node.id;
			if (node.focusable)
				return node.id;
			index--;
		}
		return semantic == null ? id : semantic;
	}

	static function visualName(kind:Int):String {
		return switch kind {
			case 1: "Box";
			case 2: "Text";
			case 3: "Image";
			case 4: "Custom";
			default: "Unknown";
		};
	}

	static function number(value:Float):String
		return Std.string(Std.int(value * 100.0) / 100.0);

	static function escape(value:String):String {
		var result = new StringBuf();
		for (index in 0...value.length) {
			switch value.charCodeAt(index) {
				case 92: result.add("\\\\");
				case 34: result.add("\\\"");
				case 10: result.add("\\n");
				case 13: result.add("\\r");
				case code: result.addChar(code);
			}
		}
		return result.toString();
	}
}
