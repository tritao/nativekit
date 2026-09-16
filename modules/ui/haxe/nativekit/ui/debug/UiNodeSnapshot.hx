package nativekit.ui.debug;

import Rect;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleInspectionEntry;

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
	public final hovered:Bool;
	public final pressed:Bool;
	public final interactionStates:Int;
	public final styleType:Null<String>;
	public final computedStyle:Null<ComputedStyle>;
	public final styleEntries:Array<StyleInspectionEntry>;
	public final matchingStyleRules:Array<StyleSource>;
	public final zIndex:Int;
	public final role:Int;
	public final semanticStates:Int;
	public final label:Null<String>;
	public final value:Null<String>;
	public final actions:Int;

	public function new(id:Int, parentId:Int, depth:Int, visualKind:Int, bounds:Rect,
			clipBounds:Rect, contentBounds:Rect, visible:Bool, enabled:Bool,
			focusable:Bool, focused:Bool, hovered:Bool, pressed:Bool, zIndex:Int, role:Int,
			semanticStates:Int, label:Null<String>, value:Null<String>, actions:Int,
			interactionStates:Int = 0, styleType:Null<String> = null,
			computedStyle:Null<ComputedStyle> = null,
			?styleEntries:Array<StyleInspectionEntry>, ?matchingStyleRules:Array<StyleSource>) {
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
		this.hovered = hovered;
		this.pressed = pressed;
		this.interactionStates = interactionStates;
		this.styleType = styleType;
		this.computedStyle = computedStyle;
		this.styleEntries = styleEntries == null ? [] : styleEntries;
		this.matchingStyleRules = matchingStyleRules == null ? [] : matchingStyleRules;
		this.zIndex = zIndex;
		this.role = role;
		this.semanticStates = semanticStates;
		this.label = label;
		this.value = value;
		this.actions = actions;
	}
}
