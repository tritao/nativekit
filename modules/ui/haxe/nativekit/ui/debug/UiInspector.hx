package nativekit.ui.debug;

import nativekit.ui.core.RenderNode;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.Semantics;

/** Produces deterministic, headless snapshots and readable render-tree dumps. */
class UiInspector {
	public static function snapshot(root:Null<RenderNode>, focused:Null<WidgetId>):Array<UiNodeSnapshot> {
		var result:Array<UiNodeSnapshot> = [];
		if (root != null)
			append(root, 0, 0, focused, result);
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
			if (record.focusable)
				line += " focusable";
			if (record.zIndex != 0)
				line += " z=" + Std.string(record.zIndex);
			if (record.role >= 0)
				line += " role=" + Std.string(record.role);
			if (record.label != null && record.label.length > 0)
				line += ' label="' + escape(record.label) + '"';
			if (record.value != null)
				line += ' value="' + escape(record.value) + '"';
			lines.push(line);
		}
		return lines.join("\n");
	}

	static function append(node:RenderNode, parentId:Int, depth:Int,
			focused:Null<WidgetId>, output:Array<UiNodeSnapshot>):Void {
		var semantics:Null<Semantics> = cast node.semantics;
		var geometry = node.resolved;
		var bounds = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.bounds();
		var clip = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.clipBounds;
		var content = geometry == null ? new Rect(0.0, 0.0, 0.0, 0.0) : geometry.contentBounds;
		var role = semantics == null ? -1 : cast semantics.role;
		output.push(new UiNodeSnapshot(node.id.value, parentId, depth,
			cast node.layout.visualKind, bounds, clip, content,
			geometry != null && geometry.visible, node.enabled, node.focusable,
			focused != null && focused.equals(node.id), node.layout.style.zIndex,
			role, semantics == null ? null : semantics.label,
			semantics == null ? null : semantics.value,
			semantics == null ? 0 : semantics.actions));
		for (child in node.children)
			append(child, node.id.value, depth + 1, focused, output);
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
		var result = StringTools.replace(value, "\\", "\\\\");
		result = StringTools.replace(result, "\"", "\\\"");
		result = StringTools.replace(result, "\n", "\\n");
		return StringTools.replace(result, "\r", "\\r");
	}
}
