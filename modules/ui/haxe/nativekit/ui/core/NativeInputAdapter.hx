package nativekit.ui.core;

import NativeKit.Handle;
import NativeKit.InputAction;
import NativeKit.TouchAction;
import NativeKit.WindowStateFlags;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEvents.NativeKitEventSubscription;

/** Routes decoded NativeKit window input into one Haxe UiContext. */
class NativeInputAdapter {
	final context:UiContext;
	final source:Handle;
	final accessibilitySource:Handle;
	final cursor:NativeCursorController;
	var attachedEvents:Null<NativeKitEvents>;
	var eventSubscription:Null<NativeKitEventSubscription>;
	final eventListener:NativeKitEventValue->Void;
	var pointerX:Float;
	var pointerY:Float;

	public function new(context:UiContext, source:Handle, ?accessibilitySource:Handle) {
		if (context == null || !source.isValid())
			throw "Native input requires a UI context and window handle";
		this.context = context;
		this.source = source;
		this.accessibilitySource = accessibilitySource == null ? source : accessibilitySource;
		cursor = new NativeCursorController(source);
		attachedEvents = null;
		eventSubscription = null;
		eventListener = function(event) { consume(event); };
		pointerX = 0.0;
		pointerY = 0.0;
	}

	/** Subscribes this adapter to the runtime's shared event pump. */
	public function attach(events:NativeKitEvents):Void {
		if (events == null)
			throw "Native input requires a NativeKit event pump";
		if (events.isDisposed())
			throw "Native input cannot attach to a disposed NativeKit event pump";
		if (attachedEvents == events)
			return;
		detach();
		eventSubscription = events.listen(eventListener);
		attachedEvents = events;
		context.setCursorHandler(function(shape) { cursor.apply(shape); });
	}

	/** Stops routing events from the attached pump. */
	public function detach():Void {
		if (attachedEvents != null) {
			if (eventSubscription != null)
				eventSubscription.dispose();
			eventSubscription = null;
			attachedEvents = null;
			context.setCursorHandler(null);
			cursor.reset();
		}
	}

	/** Consumes recognized input for this window; other event kinds/sources pass through. */
	public function consume(event:NativeKitEventValue):Bool {
		if (event == null)
			return false;
		return switch (event) {
			case ClipboardText(_, _, _) if (context.clipboard.consume(event)):
				true;
			case PointerMove(eventSource, x, y) if (matches(eventSource)):
				pointerX = x;
				pointerY = y;
				context.pointerMove(x, y);
				true;
			case PointerButton(eventSource, button, action, modifiers, x, y) if (matches(eventSource)):
				pointerX = x;
				pointerY = y;
				if (action == InputAction.Press)
					context.pointerDown(x, y, button, modifiers);
				else if (action == InputAction.Release)
					context.pointerUp(x, y, button, modifiers);
				action == InputAction.Press || action == InputAction.Release;
			case PointerScroll(eventSource, deltaX, deltaY) if (matches(eventSource)):
				context.scroll(pointerX, pointerY, deltaX, deltaY);
				true;
			case PointerEnter(eventSource, entered) if (matches(eventSource)):
				if (!entered)
					context.pointerLeave();
				true;
			case WindowStateChanged(eventSource, flags) if (matches(eventSource)):
				if ((flags & WindowStateFlags.Active) == 0)
					context.windowFocusLost();
				true;
			case NativeKitEventValue.Key(eventSource, key, scancode, action, modifiers)
				if (matches(eventSource)):
				var kind = keyKind(action);
				if (kind == null)
					false;
				else {
					context.key(kind, key, modifiers, scancode);
					true;
				}
			case TextInput(eventSource, codepoint) if (matches(eventSource)):
				context.text(UiEventKind.TextInput, fromCodepoint(codepoint), codepoint);
				true;
			case TextEdit(eventSource, edit) if (matches(eventSource)):
				context.text(UiEventKind.TextEdit, edit.text, edit);
				true;
			case AccessibilityAction(eventSource, nodeId, action, value, selectionStart,
				selectionEnd, granularity) if (eventSource == accessibilitySource):
				context.accessibilityAction(nodeId, action, value, selectionStart,
					selectionEnd, granularity);
				true;
			case Touch(eventSource, pointerId, action, tool, modifiers, x, y, pressure, tiltX, tiltY)
				if (matches(eventSource)):
				pointerX = x;
				pointerY = y;
				var routedId = touchPointerId(pointerId);
				var data = new UiTouchData(tool, pressure, tiltX, tiltY);
				if (action == TouchAction.Begin)
					context.pointerDown(x, y, 0, modifiers, routedId, data);
				else if (action == TouchAction.Move)
					context.pointerMove(x, y, modifiers, routedId, data);
				else if (action == TouchAction.End)
					context.pointerUp(x, y, 0, modifiers, routedId, data);
				else if (action == TouchAction.Cancel)
					context.pointerCancel(routedId, x, y, modifiers, data);
				action == TouchAction.Begin || action == TouchAction.Move ||
					action == TouchAction.End || action == TouchAction.Cancel;
			case _:
				false;
		}
	}

	function matches(eventSource:Handle):Bool
		return eventSource == source;

	static function keyKind(action:InputAction):Null<String> {
		if (action == InputAction.Press)
			return UiEventKind.KeyDown;
		if (action == InputAction.Release)
			return UiEventKind.KeyUp;
		if (action == InputAction.Repeat)
			return UiEventKind.KeyRepeat;
		return null;
	}

	static function touchPointerId(pointerId:Int):Int
		return pointerId | 0x80000000;

	static function fromCodepoint(codepoint:Int):String {
		if (codepoint < 0 || codepoint > 0x10ffff ||
			(codepoint >= 0xd800 && codepoint <= 0xdfff))
			codepoint = 0xfffd;
		var bytes = haxe.io.Bytes.alloc(codepoint <= 0x7f ? 1 : codepoint <= 0x7ff ? 2 :
			codepoint <= 0xffff ? 3 : 4);
		if (bytes.length == 1)
			bytes.set(0, codepoint);
		else if (bytes.length == 2) {
			bytes.set(0, 0xc0 | (codepoint >>> 6));
			bytes.set(1, 0x80 | (codepoint & 0x3f));
		} else if (bytes.length == 3) {
			bytes.set(0, 0xe0 | (codepoint >>> 12));
			bytes.set(1, 0x80 | ((codepoint >>> 6) & 0x3f));
			bytes.set(2, 0x80 | (codepoint & 0x3f));
		} else {
			bytes.set(0, 0xf0 | (codepoint >>> 18));
			bytes.set(1, 0x80 | ((codepoint >>> 12) & 0x3f));
			bytes.set(2, 0x80 | ((codepoint >>> 6) & 0x3f));
			bytes.set(3, 0x80 | (codepoint & 0x3f));
		}
		return bytes.toString();
	}
}
