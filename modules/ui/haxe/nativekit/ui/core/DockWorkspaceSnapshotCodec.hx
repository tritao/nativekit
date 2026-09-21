package nativekit.ui.core;

import haxe.Json;

/** Stable JSON representation for versioned dock workspace snapshots. */
class DockWorkspaceSnapshotCodec {
	public static function encode(snapshot:DockWorkspaceSnapshot):String {
		if (snapshot == null)
			throw "Cannot encode a null dock workspace snapshot";
		var error = DockNodeTools.validate(snapshot.root, null);
		if (error != null)
			throw error;
		return Json.stringify({
			version: snapshot.version,
			activePanelId: snapshot.activePanelId,
			root: encodeNode(snapshot.root)
		});
	}

	/** Returns null for malformed, unsupported, or structurally invalid data. */
	public static function decode(source:String):Null<DockWorkspaceSnapshot> {
		if (source == null || StringTools.trim(source).length == 0)
			return null;
		try {
			var raw:Dynamic = Json.parse(source);
			var version = Std.int(Reflect.field(raw, "version"));
			if (version != DockWorkspaceSnapshot.CurrentVersion)
				return null;
			var root = decodeNode(Reflect.field(raw, "root"));
			if (root == null || DockNodeTools.validate(root, null) != null)
				return null;
			var activeValue:Dynamic = Reflect.field(raw, "activePanelId");
			var activePanelId:Null<String> = activeValue == null ? null : castString(activeValue);
			if (activeValue != null && activePanelId == null)
				return null;
			return new DockWorkspaceSnapshot(root, activePanelId, version);
		} catch (_:Dynamic)
			return null;
	}

	static function encodeNode(node:DockNode):Dynamic {
		return switch (node) {
			case DockNode.Empty: {kind: "empty"};
			case DockNode.Panel(panelId): {kind: "panel", id: panelId};
			case DockNode.Tabs(panelIds, activePanelId):
				var encodedPanelIds:Array<String> = panelIds == null ? [] : panelIds.copy();
				{kind: "tabs", panels: encodedPanelIds, active: activePanelId};
			case DockNode.Split(axis, ratio, first, second):
				{kind: "split", axis: axisName(axis), ratio: ratio,
					first: encodeNode(first), second: encodeNode(second)};
		};
	}

	static function decodeNode(raw:Dynamic):Null<DockNode> {
		if (raw == null)
			return null;
		var kind = castString(Reflect.field(raw, "kind"));
		if (kind == null)
			return null;
		return switch (kind) {
			case "empty": DockNode.Empty;
			case "panel":
				var panelId = castString(Reflect.field(raw, "id"));
				panelId == null ? null : DockNode.Panel(panelId);
			case "tabs":
				var rawPanels:Dynamic = Reflect.field(raw, "panels");
				var activePanelId = castString(Reflect.field(raw, "active"));
				if (rawPanels == null || !Std.isOfType(rawPanels, Array) || activePanelId == null)
					null;
				else {
					var panelIds:Array<String> = [];
					var encodedPanels:Array<Dynamic> = cast rawPanels;
					for (rawPanel in encodedPanels) {
						var panelId = castString(rawPanel);
						if (panelId == null)
							return null;
						panelIds.push(panelId);
					}
					DockNode.Tabs(panelIds, activePanelId);
				}
			case "split":
				var axisNameValue = castString(Reflect.field(raw, "axis"));
				var ratioValue:Dynamic = Reflect.field(raw, "ratio");
				var first = decodeNode(Reflect.field(raw, "first"));
				var second = decodeNode(Reflect.field(raw, "second"));
				if (axisNameValue == null || !isNumber(ratioValue) || first == null || second == null)
					null;
				else {
					var axis = parseAxis(axisNameValue);
					axis == null ? null : DockNode.Split(axis, cast ratioValue, first, second);
				}
			default: null;
		};
	}

	static function castString(value:Dynamic):Null<String>
		return value == null || !Std.isOfType(value, String) ? null : cast value;

	static function isNumber(value:Dynamic):Bool
		return Std.isOfType(value, Int) || Std.isOfType(value, Float);

	static function axisName(axis:DockSplitAxis):String
		return axis == DockSplitAxis.Horizontal ? "horizontal" : "vertical";

	static function parseAxis(value:String):Null<DockSplitAxis>
		return value == "horizontal" ? DockSplitAxis.Horizontal :
			value == "vertical" ? DockSplitAxis.Vertical : null;
}
