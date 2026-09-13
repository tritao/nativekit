package nativekit.ui.debug;

import Rect;

/** Stable headless inspection record for one resolved Haxe render node. */
class UiNodeSnapshot {
	public final id:Int;
	public final parentId:Int;
	public final depth:Int;
	public final visualKind:Int;
	public final bounds:Rect;
	public final clipBounds:Rect;
	public final contentBounds:Rect;
	public final visible:Bool;
	public final enabled:Bool;
	public final focusable:Bool;
	public final focused:Bool;
	public final zIndex:Int;
	public final role:Int;
	public final label:Null<String>;
	public final value:Null<String>;
	public final actions:Int;

	public function new(id:Int, parentId:Int, depth:Int, visualKind:Int, bounds:Rect,
			clipBounds:Rect, contentBounds:Rect, visible:Bool, enabled:Bool,
			focusable:Bool, focused:Bool, zIndex:Int, role:Int, label:Null<String>,
			value:Null<String>, actions:Int) {
		this.id = id;
		this.parentId = parentId;
		this.depth = depth;
		this.visualKind = visualKind;
		this.bounds = bounds;
		this.clipBounds = clipBounds;
		this.contentBounds = contentBounds;
		this.visible = visible;
		this.enabled = enabled;
		this.focusable = focusable;
		this.focused = focused;
		this.zIndex = zIndex;
		this.role = role;
		this.label = label;
		this.value = value;
		this.actions = actions;
	}
}
