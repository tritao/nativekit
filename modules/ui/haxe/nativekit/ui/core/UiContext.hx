package nativekit.ui.core;

import LayoutFrame;
import LayoutSession;
import Canvas;
import DisplayList;
import Renderer;
import Surface;
import FrameInfo;
import ResolvedLayoutItem;
import FontCollection;
import NativeKitSurface;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.Theme;
import nativekit.ui.gestures.GestureArena;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.debug.AccessibilityAudit;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.debug.UiInspector;
import nativekit.ui.debug.UiNodeSnapshot;

/** Owns the frame-local render tree and the Haxe-side UI subsystems. */
class UiContext {
	final session:LayoutSession;
	public final stateStore:StateStore;
	public final clipboard:ClipboardService;
	public final buildContext:BuildContext;
	public final focus:FocusManager;
	public final events:EventDispatcher;
	public final gestures:GestureArena;
	public final animations:AnimationScheduler;
	public var root(default, null):Null<RenderNode>;
	var submittedStateRevision:Int;
	var disposed:Bool;
	var customCanvases:Map<Int, Canvas>;
	var customLists:Map<Int, DisplayList>;
	var accessibilityBridge:Null<AccessibilityBridge>;
	var accessibilitySurface:Null<NativeKitSurface>;
	var diagnosticStage:Int = 0;
	public final textInput:TextInputBridge;

	public function new(?session:LayoutSession, ?fonts:FontCollection, ?theme:Theme) {
		this.session = session == null ? LayoutSession.create() : session;
		stateStore = new StateStore();
		clipboard = new ClipboardService();
		textInput = new TextInputBridge();
		gestures = new GestureArena();
		animations = new AnimationScheduler();
		buildContext = new BuildContext(stateStore, fonts, textInput, clipboard, theme,
			gestures, animations);
		if (fonts != null)
			this.session.setFonts(fonts);
		focus = new FocusManager();
		events = new EventDispatcher(focus);
		root = null;
		submittedStateRevision = -1;
		disposed = false;
		buildContext.setFocusRequester(function(id) { return focusWidget(id); });
		customCanvases = new Map();
		customLists = new Map();
		accessibilityBridge = null;
		accessibilitySurface = null;
	}

	/** Sets fonts for text-aware widgets and the native layout session. */
	public function setFonts(fonts:FontCollection):Void {
		ensureLive();
		if (fonts == null || fonts.isDisposed())
			throw "UI context requires a live font collection";
		session.setFonts(fonts);
		buildContext.setFonts(fonts);
	}

	/** Sets the palette used by subsequent view builds. */
	public function setTheme(theme:Theme):Void {
		ensureLive();
		buildContext.setTheme(theme);
	}

	/** Attaches the host surface used by platform text-input synchronization. */
	public function attachPlatformSurface(surface:NativeKitSurface):Void {
		ensureLive();
		if (surface == null || surface.isDisposed())
			throw "UI context requires a live NativeKit surface";
		buildContext.setPlatformSurface(surface);
	}

	/** Builds a fresh view tree, resolves native layout, and reconnects geometry by stable ID. */
	public function submit(view:View, frame:LayoutFrame):RenderNode {
		diagnosticStage = 1;
		ensureLive();
		if (view == null || frame == null)
			throw "A UI frame requires a view and layout frame";
		diagnosticStage = 2;
		gestures.advance(frame.deltaSeconds);
		diagnosticStage = 3;
		animations.advance(frame.deltaSeconds);
		buildContext.beginFrame();
		diagnosticStage = 4;
		var next = buildContext.withScope(new Key("root"), function() return view.build(buildContext));
		if (next == null || next.parent != null)
			throw "A view must produce one unparented render tree root";
		diagnosticStage = 5;
		var resolved = session.submit(next.layout, frame);
		diagnosticStage = 6;
		var byId = new Map<Int, ResolvedLayoutItem>();
		for (item in resolved)
			byId.set(item.id, item);
		var resolvedStateRevision = stateStore.revision;
		var missing = false;
		diagnosticStage = 7;
		next.walk(function(node) {
			// Geometry is keyed by the exact LayoutNode ID serialized to NativeUI.
			node.setResolved(byId.get(node.layout.id));
			if (node.resolved == null)
				missing = true;
		});
		if (missing) {
			diagnosticStage = 8;
			throw "Native layout did not return geometry for every render node";
		}
		diagnosticStage = 9;
		var previousFocus = focus.focusedId;
		focus.rebuild(next);
		var nextFocus = focus.focusedId;
		if (previousFocus != null && (nextFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(previousFocus, UiEventKind.Blur);
		root = next;
		events.setRoot(next);
		gestures.setRoot(next);
		if (nextFocus != null && (previousFocus == null || !previousFocus.equals(nextFocus)))
			events.focusEvent(nextFocus, UiEventKind.Focus);
		submittedStateRevision = resolvedStateRevision;
		if (accessibilityBridge != null)
			accessibilityBridge.update(next, focus.focusedId);
		diagnosticStage = 0;
		return next;
	}

	public function getDiagnosticStage():Int
		return diagnosticStage;

	/** Connects this frame's semantic projection to a NativeKit platform surface. */
	public function updateAccessibility(surface:NativeKitSurface):Void {
		ensureLive();
		if (surface == null || surface.isDisposed())
			throw "Accessibility projection requires a live NativeKit surface";
		if (accessibilitySurface != surface) {
			if (accessibilityBridge != null)
				accessibilityBridge.dispose();
			accessibilitySurface = surface;
			accessibilityBridge = new AccessibilityBridge(surface);
		}
		accessibilityBridge.update(root, focus.focusedId);
	}

	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		if (root == null)
			throw "Submit a view before rendering the UI context";
		session.clearCustomPaints();
		var painted = new Map<Int, Bool>();
		root.walk(function(node) {
			if (!node.hasPaintHandler() || node.resolved == null ||
				node.resolved.clipBounds.width <= 0.0 || node.resolved.clipBounds.height <= 0.0)
				return;
			var nodeId = node.id.value;
			var canvas = customCanvases.get(nodeId);
			if (canvas == null) {
				canvas = new Canvas();
				customCanvases.set(nodeId, canvas);
			}
			canvas.reset();
			var geometry:ResolvedLayoutItem = cast node.resolved;
			canvas.withState(function(target) {
				target.resetTransform();
				target.clip(geometry.clipBounds);
				target.setTransform(geometry.transform);
				target.translate(geometry.x, geometry.y);
				node.paint(target);
			});
			var displayList = customLists.get(nodeId);
			if (displayList == null) {
				displayList = DisplayList.create();
				customLists.set(nodeId, displayList);
			}
			canvas.update(displayList);
			session.setCustomPaint(nodeId, displayList);
			painted.set(nodeId, true);
		});
		var stale:Array<Int> = [];
		for (nodeId in customLists.keys())
			if (!painted.exists(nodeId))
				stale.push(nodeId);
		for (nodeId in stale) {
			var canvas = customCanvases.get(nodeId);
			if (canvas != null)
				canvas.reset();
			var displayList = customLists.get(nodeId);
			if (displayList != null)
				displayList.dispose();
			customCanvases.remove(nodeId);
			customLists.remove(nodeId);
		}
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

	/** Routes a NativeKit platform accessibility action to the addressed semantic node. */
	public function accessibilityAction(id:Int, action:Int, value:Null<String>,
			selectionStart:Int, selectionEnd:Int, granularity:Int):Bool {
		ensureLive();
		if (root == null || id <= 0)
			return false;
		var widgetId = new WidgetId(id);
		var node = root.find(widgetId);
		if (node == null || node.semantics == null || node.resolved == null || !node.resolved.visible)
			return false;
		var semantics:Semantics = cast node.semantics;
		var request = AccessibilityRequest.create(action, value, selectionStart,
			selectionEnd, granularity);
		if (request == null)
			return false;
		var enabled = enabledAlongPath(node);
		var actions = semantics.actions;
		if (node.focusable && enabled)
			actions |= nativekit.ui.semantics.AccessibilityAction.Focus;
		if (!enabled)
			actions = 0;
		if ((actions & request.capability) == 0)
			return false;
		var geometry:ResolvedLayoutItem = cast node.resolved;
		if (action == AccessibilityRequest.Focus)
			return focusWidget(widgetId);
		if (action == AccessibilityRequest.ClearFocus) {
			if (focus.focusedId != null && focus.focusedId.equals(widgetId))
				clearFocus();
			return true;
		}
		var deltaX = 0.0;
		var deltaY = 0.0;
		if (action == AccessibilityRequest.ScrollForward)
			deltaY = -geometry.height;
		else if (action == AccessibilityRequest.ScrollBackward)
			deltaY = geometry.height;
		if (action == AccessibilityRequest.ScrollForward ||
			action == AccessibilityRequest.ScrollBackward)
			return events.targetEvent(UiEventKind.Scroll, widgetId, null, deltaX,
				deltaY, new AccessibilityActionData(action, selectionStart, selectionEnd, granularity));
		return events.targetEvent(request.kind, widgetId, request.value, 0.0, 0.0,
			new AccessibilityActionData(action, selectionStart, selectionEnd, granularity));
	}

	/** Clears transient pointer state and reports loss of platform window focus. */
	public function windowFocusLost():Void {
		ensureLive();
		events.cancelPointers();
		gestures.cancelAll();
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
			pointerId:Int = 0, data:Dynamic = null, timestamp:Float = -1.0):Void {
		ensureLive();
		events.pointerDown(x, y, button, modifiers, pointerId, data, timestamp);
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

	/** Returns a deterministic headless snapshot of the most recently submitted tree. */
	public function inspect():Array<UiNodeSnapshot> {
		ensureLive();
		return UiInspector.snapshot(root, focus.focusedId, events.hoveredId(), events.pressedId());
	}

	/** Formats the current render and semantic tree for logs or developer tools. */
	public function dumpTree():String {
		ensureLive();
		return UiInspector.dump(root, focus.focusedId);
	}

	/** Audits common accessibility naming and focus-geometry mistakes. */
	public function auditAccessibility():Array<AccessibilityIssue> {
		ensureLive();
		return AccessibilityAudit.inspect(root);
	}

	public function dispose():Void {
		if (disposed)
			return;
		session.clearCustomPaints();
		for (nodeId in customCanvases.keys()) {
			var canvas = customCanvases.get(nodeId);
			if (canvas != null)
				canvas.reset();
		}
		for (nodeId in customLists.keys()) {
			var displayList = customLists.get(nodeId);
			if (displayList != null)
				displayList.dispose();
		}
		customCanvases = new Map();
		customLists = new Map();
		session.dispose();
		clipboard.dispose();
		textInput.dispose();
		gestures.cancelAll();
		animations.cancelAll();
		if (accessibilityBridge != null)
			accessibilityBridge.dispose();
		stateStore.dispose();
		disposed = true;
		root = null;
		accessibilityBridge = null;
		accessibilitySurface = null;
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

	static function enabledAlongPath(node:RenderNode):Bool {
		var current:Null<RenderNode> = node;
		while (current != null) {
			var present:RenderNode = cast current;
			if (!present.enabled)
				return false;
			current = present.parent;
		}
		return true;
	}
}
