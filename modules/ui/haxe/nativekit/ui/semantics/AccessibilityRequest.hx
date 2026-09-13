package nativekit.ui.semantics;

import nativekit.ui.core.UiEventKind;

/** Decoded platform accessibility request routed to a Haxe semantic target. */
class AccessibilityRequest {
	public static inline var Activate = 1;
	public static inline var Focus = 2;
	public static inline var ClearFocus = 3;
	public static inline var SetValue = 4;
	public static inline var SetSelection = 5;
	public static inline var Increment = 6;
	public static inline var Decrement = 7;
	public static inline var ScrollForward = 8;
	public static inline var ScrollBackward = 9;
	public static inline var MoveNext = 10;
	public static inline var MovePrevious = 11;

	public final action:Int;
	public final capability:Int;
	public final kind:String;
	public final value:Null<String>;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final granularity:Int;

	function new(action:Int, capability:Int, kind:String, value:Null<String>,
			selectionStart:Int, selectionEnd:Int, granularity:Int) {
		this.action = action;
		this.capability = capability;
		this.kind = kind;
		this.value = value;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.granularity = granularity;
	}

	public static function create(action:Int, value:Null<String>, selectionStart:Int,
			selectionEnd:Int, granularity:Int):Null<AccessibilityRequest> {
		var capability:Int;
		var kind:String;
		switch (action) {
			case Activate: capability = AccessibilityAction.Activate; kind = UiEventKind.Activate;
			case Focus, ClearFocus: capability = AccessibilityAction.Focus; kind = "";
			case SetValue: capability = AccessibilityAction.SetValue; kind = UiEventKind.AccessibilitySetValue;
			case SetSelection: capability = AccessibilityAction.SetSelection; kind = UiEventKind.AccessibilitySetSelection;
			case Increment: capability = AccessibilityAction.Increment; kind = UiEventKind.AccessibilityIncrement;
			case Decrement: capability = AccessibilityAction.Decrement; kind = UiEventKind.AccessibilityDecrement;
			case ScrollForward: capability = AccessibilityAction.ScrollForward; kind = UiEventKind.Scroll;
			case ScrollBackward: capability = AccessibilityAction.ScrollBackward; kind = UiEventKind.Scroll;
			case MoveNext: capability = AccessibilityAction.MoveNext; kind = UiEventKind.AccessibilityMoveNext;
			case MovePrevious: capability = AccessibilityAction.MovePrevious; kind = UiEventKind.AccessibilityMovePrevious;
			default: return null;
		}
		return new AccessibilityRequest(action, capability, kind, value,
			selectionStart, selectionEnd, granularity);
	}
}
