package nativekit.ui.core;

import LayoutFrame;
import LayoutSession;
import Renderer;
import Surface;
import FrameInfo;
import ResolvedLayoutItem;

/** Owns the frame-local render tree and the Haxe-side UI subsystems. */
class UiContext {
	final session:LayoutSession;
	public final stateStore:StateStore;
	public final buildContext:BuildContext;
	public final focus:FocusManager;
	public final events:EventDispatcher;
	public var root(default, null):Null<RenderNode>;
	var submittedStateRevision:Int;
	var disposed:Bool;

	public function new(?session:LayoutSession) {
		this.session = session == null ? LayoutSession.create() : session;
		stateStore = new StateStore();
		buildContext = new BuildContext(stateStore);
		focus = new FocusManager();
		events = new EventDispatcher(focus);
		root = null;
		submittedStateRevision = -1;
		disposed = false;
	}

	/** Builds a fresh view tree, resolves native layout, and reconnects geometry by stable ID. */
	public function submit(view:View, frame:LayoutFrame):RenderNode {
		ensureLive();
		if (view == null || frame == null)
			throw "A UI frame requires a view and layout frame";
		buildContext.beginFrame();
		var next = buildContext.withScope(new Key("root"), function() return view.build(buildContext));
		if (next == null || next.parent != null)
			throw "A view must produce one unparented render tree root";
		var resolved = session.submit(next.layout, frame);
		var byId = new Map<Int, ResolvedLayoutItem>();
		for (item in resolved)
			byId.set(item.id, item);
		var missing = false;
		next.walk(function(node) {
			node.resolved = byId.get(node.id.value);
			if (node.resolved == null)
				missing = true;
		});
		if (missing)
			throw "Native layout did not return geometry for every render node";
		var previousFocus = focus.focusedId;
		focus.rebuild(next);
		var nextFocus = focus.focusedId;
		if (previousFocus != null && (nextFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(previousFocus, UiEventKind.Blur);
		root = next;
		events.setRoot(next);
		if (nextFocus != null && (previousFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(nextFocus, UiEventKind.Focus);
		submittedStateRevision = stateStore.revision;
		return next;
	}

	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		if (root == null)
			throw "Submit a view before rendering the UI context";
		session.render(renderer, surface, frame);
	}

	public function focusWidget(id:WidgetId):Bool {
		ensureLive();
		var previous = focus.focusedId;
		if (!focus.focus(id))
			return false;
		dispatchFocusChange(previous, focus.focusedId);
		return true;
	}

	public function focusNext():Null<WidgetId> {
		ensureLive();
		var previous = focus.focusedId;
		var next = focus.focusNext();
		dispatchFocusChange(previous, next);
		return next;
	}

	public function focusPrevious():Null<WidgetId> {
		ensureLive();
		var previous = focus.focusedId;
		var next = focus.focusPrevious();
		dispatchFocusChange(previous, next);
		return next;
	}

	public function clearFocus():Void {
		ensureLive();
		var previous = focus.focusedId;
		focus.focus(null);
		dispatchFocusChange(previous, null);
	}

	/** Clears transient pointer state and reports loss of platform window focus. */
	public function windowFocusLost():Void {
		ensureLive();
		events.cancelPointers();
		var previous = focus.focusedId;
		if (previous != null)
			events.focusEvent(previous, UiEventKind.FocusLost);
		focus.focus(null);
		dispatchFocusChange(previous, null);
	}

	public function pointerMove(x:Float, y:Float, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerMove(x, y, modifiers, pointerId, data);
	}

	public function pointerDown(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerDown(x, y, button, modifiers, pointerId, data);
	}

	public function pointerUp(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerUp(x, y, button, modifiers, pointerId, data);
	}

	public function pointerCancel(pointerId:Int, x:Float, y:Float,
			modifiers:Int = 0, data:Dynamic = null):Void {
		ensureLive();
		events.pointerCancel(pointerId, x, y, modifiers, data);
	}

	public function pointerLeave(pointerId:Int = 0):Void {
		ensureLive();
		events.clearPointer(pointerId);
	}

	public function scroll(x:Float, y:Float, deltaX:Float, deltaY:Float, modifiers:Int = 0):Void {
		ensureLive();
		events.scroll(x, y, deltaX, deltaY, modifiers);
	}

	public function key(kind:String, key:Int, modifiers:Int = 0, scancode:Int = 0):Void {
		ensureLive();
		events.key(kind, key, modifiers, scancode);
	}

	public function text(kind:String, value:Null<String>, data:Dynamic = null):Void {
		ensureLive();
		events.text(kind, value, data);
	}

	public function isDirty():Bool
		return stateStore.revision != submittedStateRevision;

	public function dispose():Void {
		if (disposed)
			return;
		session.dispose();
		disposed = true;
		root = null;
	}

	function dispatchFocusChange(previous:Null<WidgetId>, next:Null<WidgetId>):Void {
		if (previous != null && (next == null || !previous.equals(next)))
			events.focusEvent(previous, UiEventKind.Blur);
		if (next != null && (previous == null || !previous.equals(next)))
			events.focusEvent(next, UiEventKind.Focus);
	}

	function ensureLive():Void {
		if (disposed)
			throw "UI context has been disposed";
	}
}
