package nativekit.ui.core;

import NativeKit;
import nativekit.ui.core.CursorShape as UiCursorShape;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;

/** Capture/target/bubble dispatch with per-pointer hover and pointer capture. */
class EventDispatcher {
	var root:Null<RenderNode>;
	final focus:FocusManager;
	final interactionStates:InteractionStateStore;
	var commandRegistry:Null<CommandRegistry>;
	var commandContext:CommandContext;
	var hoverPaths:Map<Int, Array<RenderNode>>;
	final pointerLocations:Map<Int, PointerLocation>;
	final pressedIds:Map<Int, WidgetId>;
	final capturedIds:Map<Int, WidgetId>;
	final suppressedClicks:Map<Int, Bool>;
	var hitTestProvider:Null<Float->Float->Array<RenderNode>>;
	var pointerCaptureHandler:Null<Bool->Void>;
	var platformPointerCaptured:Bool;

	public function new(focus:FocusManager, ?interactionStates:InteractionStateStore,
			?commandRegistry:CommandRegistry, ?commandContext:CommandContext) {
		this.focus = focus;
		this.interactionStates = interactionStates == null ? new InteractionStateStore() : interactionStates;
		this.commandRegistry = commandRegistry;
		this.commandContext = commandContext == null ? new CommandContext() : commandContext;
		root = null;
		hoverPaths = new Map();
		pointerLocations = new Map();
		pressedIds = new Map();
		capturedIds = new Map();
		suppressedClicks = new Map();
		hitTestProvider = null;
		pointerCaptureHandler = null;
		platformPointerCaptured = false;
	}

	/** Installs the application-level shortcut route used after widget dispatch. */
	public function setCommandRegistry(registry:Null<CommandRegistry>):Void
		commandRegistry = registry;

	/** Updates the context passed to command shortcuts. */
	public function setCommandContext(context:Null<CommandContext>):Void
		commandContext = context == null ? new CommandContext() : context;

	/** Installs the current resolved-scene geometric picker. */
	public function setHitTestProvider(provider:Null<Float->Float->Array<RenderNode>>):Void
		hitTestProvider = provider;

	/** Returns the active geometric path; useful for diagnostics and parity tests. */
	public function hitTestPath(x:Float, y:Float):Array<RenderNode>
		return hitPath(x, y);

	/** Installs the host bridge for physical window/surface pointer capture. */
	public function setPointerCaptureHandler(handler:Null<Bool->Void>):Void {
		if (pointerCaptureHandler != null && platformPointerCaptured)
			pointerCaptureHandler(false);
		pointerCaptureHandler = handler;
		platformPointerCaptured = false;
		syncPlatformPointerCapture();
	}

	public function setRoot(root:Null<RenderNode>):Void {
		this.root = root;
		var stale:Array<Int> = [];
		for (pointerId in capturedIds.keys()) {
			var id = capturedIds.get(pointerId);
			if (root == null || id == null || root.find(id) == null)
				stale.push(pointerId);
		}
		for (pointerId in stale)
			clearPointerCapture(pointerId);
		stale = [];
		for (pointerId in pressedIds.keys()) {
			var id = pressedIds.get(pointerId);
			if (root == null || id == null || root.find(id) == null)
				stale.push(pointerId);
		}
		for (pointerId in stale)
			pressedIds.remove(pointerId);
		for (pointerId in pointerLocations.keys()) {
			var location = pointerLocations.get(pointerId);
			if (location == null)
				continue;
			var x = location.x;
			var y = location.y;
			var path = capturedPath(pointerId);
			if (path.length == 0)
				path = hitPath(x, y);
			updateHover(pointerId, path, x, y);
		}
	}

	public function pointerMove(x:Float, y:Float, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		rememberPointer(pointerId, x, y);
		var path = hitPath(x, y);
		var targetPath = capturedPath(pointerId);
		if (targetPath.length == 0)
			targetPath = path;
		updateHover(pointerId, targetPath, x, y);
		if (targetPath.length > 0) {
			var event = new UiEvent(UiEventKind.PointerMove,
				targetPath[targetPath.length - 1].id, x, y, 0.0, 0.0, 0, 0,
				modifiers, null, data, 0, pointerId);
			dispatchPath(targetPath, event);
			applyPointerCaptureRequest(pointerId, event);
			if (event.pointerReleaseRequested)
				updateHover(pointerId, path, x, y);
		}
	}

	public function pointerDown(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null, timestamp:Float = -1.0):Void {
		rememberPointer(pointerId, x, y);
		var path = hitPath(x, y);
		if (root != null)
			dispatchOutsidePointerDown(path, x, y, button, modifiers, pointerId, data, timestamp);
		if (path.length == 0)
			return;
		var target = path[path.length - 1];
		pressedIds.set(pointerId, target.id);
		var index = path.length - 1;
		while (index >= 0) {
			if (path[index].focusable && path[index].enabled && changeFocus(path[index].id))
				break;
			index--;
		}
		var event = new UiEvent(UiEventKind.PointerDown, target.id, x, y,
			0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId,
			timestamp < 0.0 ? NativeKit.nk_time_seconds() : timestamp);
		updatePathState(path, StyleState.Pressed, true);
		dispatchPath(path, event);
		if (event.defaultPrevented)
			suppressedClicks.set(pointerId, true);
		applyPointerCaptureRequest(pointerId, event);
	}

	function dispatchOutsidePointerDown(path:Array<RenderNode>, x:Float, y:Float,
			button:Int, modifiers:Int, pointerId:Int, data:Dynamic, timestamp:Float):Void {
		var target = path.length == 0 ? root : path[path.length - 1];
		if (target == null)
			return;
		var event = new UiEvent(UiEventKind.PointerDown, target.id, x, y, 0.0, 0.0,
			button, 0, modifiers, null, data, 0, pointerId,
			timestamp < 0.0 ? NativeKit.nk_time_seconds() : timestamp);
		root.walk(function(node) {
			if (event.propagationStopped || contains(path, node))
				return;
			event.setCurrentTarget(node);
			node.invokePointerDownOutside(event);
		});
	}

	static function contains(path:Array<RenderNode>, node:RenderNode):Bool {
		for (entry in path)
			if (entry == node)
				return true;
		return false;
	}

	public function pointerUp(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		rememberPointer(pointerId, x, y);
		var releasePath = hitPath(x, y);
		var path = capturedPath(pointerId);
		var pressed = pressedIds.get(pointerId);
		var clickSuppressed = suppressedClicks.exists(pointerId);
		if (path.length == 0)
			path = pressedPath(pointerId);
		clearPointerCapture(pointerId);
		pressedIds.remove(pointerId);
		suppressedClicks.remove(pointerId);
		if (path.length > 0) {
			var target = path[path.length - 1];
			updatePathState(path, StyleState.Pressed, false);
			dispatchPath(path, new UiEvent(UiEventKind.PointerUp, target.id, x, y,
				0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId));
		}
		if (!clickSuppressed && pressed != null && releasePath.length > 0 &&
			releasePath[releasePath.length - 1].id.equals(pressed)) {
			dispatchPath(releasePath, new UiEvent(UiEventKind.Click, pressed, x, y,
				0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId));
		}
		updateHover(pointerId, releasePath, x, y);
	}

	public function pointerCancel(pointerId:Int, x:Float, y:Float,
			modifiers:Int = 0, data:Dynamic = null):Void {
		var path = capturedPath(pointerId);
		if (path.length == 0)
			path = pressedPath(pointerId);
		clearPointerCapture(pointerId);
		pressedIds.remove(pointerId);
		suppressedClicks.remove(pointerId);
		if (path.length > 0) {
			updatePathState(path, StyleState.Pressed, false);
			dispatchPath(path, new UiEvent(UiEventKind.PointerCancel,
				path[path.length - 1].id, x, y, 0.0, 0.0, 0, 0, modifiers,
				null, data, 0, pointerId));
		}
		updateHover(pointerId, [], x, y);
		pointerLocations.remove(pointerId);
	}

	public function scroll(x:Float, y:Float, deltaX:Float, deltaY:Float,
			modifiers:Int = 0):Void {
		var path = hitPath(x, y);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(UiEventKind.Scroll, path[path.length - 1].id,
				x, y, deltaX, deltaY, 0, 0, modifiers));
	}

	public function key(kind:String, key:Int, modifiers:Int = 0, scancode:Int = 0):Void {
		var node = focus.focusedNode();
		if (node == null) {
			if (kind == UiEventKind.KeyDown && commandRegistry != null)
				commandRegistry.dispatchContext(key, modifiers, commandContext);
			return;
		}
		var path = HitTest.pathTo(node);
		if (path.length == 0) {
			if (kind == UiEventKind.KeyDown && commandRegistry != null)
				commandRegistry.dispatchContext(key, modifiers, commandContext);
			return;
		}
		var event = new UiEvent(kind, node.id, 0.0, 0.0, 0.0, 0.0, 0, key,
			modifiers, null, null, scancode);
		dispatchPath(path, event);
		if (event.defaultPrevented)
			return;
		if (kind == UiEventKind.KeyDown && commandRegistry != null &&
			commandRegistry.dispatchContext(key, modifiers, commandContext).succeeded) {
			event.preventDefault();
			return;
		}
		if (key == UiKey.Tab &&
			(kind == UiEventKind.KeyDown || kind == UiEventKind.KeyRepeat))
			moveFocus((modifiers & UiModifier.Shift) != 0);
		else if (kind == UiEventKind.KeyDown && (key == UiKey.Enter || key == UiKey.Space))
			dispatchPath(path, new UiEvent(UiEventKind.Activate, node.id, 0.0, 0.0,
				0.0, 0.0, 0, key, modifiers, null, null, scancode));
	}

	public function text(kind:String, text:Null<String>, data:Dynamic = null):Void {
		var node = focus.focusedNode();
		if (node == null)
			return;
		var path = HitTest.pathTo(node);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(kind, node.id, 0.0, 0.0,
				0.0, 0.0, 0, 0, 0, text, data));
	}

	/** Dispatches a platform semantic action from its addressed node through capture and bubble. */
	public function targetEvent(kind:String, id:WidgetId, text:Null<String> = null,
			deltaX:Float = 0.0, deltaY:Float = 0.0, data:Dynamic = null):Bool {
		if (root == null || id == null)
			return false;
		var path = HitTest.pathTo(root.find(id));
		if (path.length == 0)
			return false;
		dispatchPath(path, new UiEvent(kind, id, 0.0, 0.0, deltaX, deltaY,
			0, 0, 0, text, data));
		return true;
	}

	public function clearPointer(pointerId:Int = 0):Void {
		updateHover(pointerId, [], 0.0, 0.0);
		clearPointerCapture(pointerId);
		pressedIds.remove(pointerId);
		suppressedClicks.remove(pointerId);
		pointerLocations.remove(pointerId);
	}

	public function cancelPointers():Void {
		var active:Array<Int> = [];
		for (pointerId in capturedIds.keys())
			active.push(pointerId);
		for (pointerId in pressedIds.keys())
			if (active.indexOf(pointerId) < 0)
				active.push(pointerId);
		for (pointerId in active)
			pointerCancel(pointerId, 0.0, 0.0);
		var hovering:Array<Int> = [];
		for (pointerId in hoverPaths.keys())
			hovering.push(pointerId);
		for (pointerId in hovering)
			clearPointer(pointerId);
	}

	public function focusEvent(id:WidgetId, kind:String):Void
		{
			var node = root == null ? null : root.find(id);
			if (node != null)
				updateDirectState(node, kind);
			dispatchDirect(id, kind);
		}

	/** Returns the deepest node currently under a pointer, if any. */
	public function hoveredId(pointerId:Int = 0):Null<WidgetId> {
		var path = hoverPaths.get(pointerId);
		return path == null || path.length == 0 ? null : path[path.length - 1].id;
	}

	/** Returns the deepest node pressed by a pointer, if any. */
	public function pressedId(pointerId:Int = 0):Null<WidgetId>
		return pressedIds.get(pointerId);

	public function hasPointerCapture(id:WidgetId, pointerId:Int = 0):Bool {
		var captured = capturedIds.get(pointerId);
		return captured != null && id != null && captured.equals(id);
	}

	/** Returns the deepest cursor intent for the hovered or captured pointer path. */
	public function cursorShape(pointerId:Int = 0):UiCursorShape {
		var path = capturedPath(pointerId);
		if (path.length == 0)
			path = hoverPaths.get(pointerId);
		if (path == null)
			return UiCursorShape.Arrow;
		var index = path.length - 1;
		while (index >= 0) {
			if (path[index].cursor != null)
				return cast path[index].cursor;
			index--;
		}
		return UiCursorShape.Arrow;
	}

	function updateHover(pointerId:Int, path:Array<RenderNode>, x:Float, y:Float):Void {
		var previous = hoverPaths.get(pointerId);
		if (previous == null)
			previous = [];
		var common = 0;
		while (common < previous.length && common < path.length &&
			previous[common].id.equals(path[common].id))
			common++;
		var index = previous.length - 1;
		while (index >= common) {
			setState(previous[index], StyleState.Hovered, false);
			dispatchPath(previous.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverLeave, previous[index].id, x, y,
					0.0, 0.0, 0, 0, 0, null, null, 0, pointerId));
			index--;
		}
		for (index in common...path.length)
			{
			setState(path[index], StyleState.Hovered, true);
			dispatchPath(path.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverEnter, path[index].id, x, y,
					0.0, 0.0, 0, 0, 0, null, null, 0, pointerId));
			}
		hoverPaths.set(pointerId, path);
	}

	function capturedPath(pointerId:Int):Array<RenderNode> {
		var id = capturedIds.get(pointerId);
		if (id == null || root == null)
			return [];
		return HitTest.pathTo(root.find(id));
	}

	function hitPath(x:Float, y:Float):Array<RenderNode> {
		if (root == null)
			return [];
		return hitTestProvider == null ? HitTest.path(root, x, y) : hitTestProvider(x, y);
	}

	function pressedPath(pointerId:Int):Array<RenderNode> {
		var id = pressedIds.get(pointerId);
		if (id == null || root == null)
			return [];
		return HitTest.pathTo(root.find(id));
	}

	function applyPointerCaptureRequest(pointerId:Int, event:UiEvent):Void {
		if (event.pointerReleaseRequested)
			clearPointerCapture(pointerId);
		else if (event.pointerCaptureTarget != null)
			setPointerCapture(pointerId, event.pointerCaptureTarget);
	}

	function setPointerCapture(pointerId:Int, id:WidgetId):Void {
		capturedIds.set(pointerId, id);
		syncPlatformPointerCapture();
	}

	function clearPointerCapture(pointerId:Int):Void {
		if (!capturedIds.exists(pointerId))
			return;
		capturedIds.remove(pointerId);
		syncPlatformPointerCapture();
	}

	function syncPlatformPointerCapture():Void {
		if (pointerCaptureHandler == null) {
			platformPointerCaptured = false;
			return;
		}
		var requested = false;
		for (_ in capturedIds.keys()) {
			requested = true;
			break;
		}
		if (requested == platformPointerCaptured)
			return;
		platformPointerCaptured = requested;
		pointerCaptureHandler(requested);
	}

	function rememberPointer(pointerId:Int, x:Float, y:Float):Void {
		pointerLocations.set(pointerId, new PointerLocation(x, y));
	}

	function changeFocus(id:WidgetId):Bool {
		var previous = focus.focusedId;
		if (previous != null && previous.equals(id))
			return true;
		if (!focus.focus(id))
			return false;
		notifyFocusChange(previous, focus.focusedId);
		return true;
	}

	function moveFocus(reverse:Bool):Void {
		var previous = focus.focusedId;
		var next = reverse ? focus.focusPrevious() : focus.focusNext();
		notifyFocusChange(previous, next);
	}

	function notifyFocusChange(previous:Null<WidgetId>, next:Null<WidgetId>):Void {
		if (previous != null && (next == null || !previous.equals(next)))
			dispatchDirect(previous, UiEventKind.Blur);
		if (next != null && (previous == null || !previous.equals(next)))
			dispatchDirect(next, UiEventKind.Focus);
	}

	function dispatchDirect(id:WidgetId, kind:String):Void {
		if (root == null)
			return;
		var node = focus.eligibleNode(id);
		if (node == null)
			node = root.find(id);
		if (node == null)
			return;
		var event = new UiEvent(kind, id);
		event.phase = "target";
		event.setCurrentTarget(node);
		node.invoke(event);
	}

	function updateDirectState(node:RenderNode, kind:String):Void {
		if (kind == UiEventKind.Focus)
			setState(node, StyleState.Focused, true);
		else if (kind == UiEventKind.Blur || kind == UiEventKind.FocusLost)
			setState(node, StyleState.Focused, false);
	}

	function updatePathState(path:Array<RenderNode>, state:StyleState, enabled:Bool):Void {
		var node = interactionOwner(path);
		if (node != null && (node.enabled || !enabled))
			setState(node, state, enabled);
	}

	function setState(node:RenderNode, state:StyleState, enabled:Bool):Void {
		var next = StyleStateUtil.withState(node.states, state, enabled);
		if (next == node.states)
			return;
		node.states = next;
		interactionStates.set(node.id, next);
	}

	static function interactionOwner(path:Array<RenderNode>):Null<RenderNode> {
		var index = path.length - 1;
		while (index >= 0) {
			if (path[index].focusable || path[index].semantics != null)
				return path[index];
			index--;
		}
		return path.length == 0 ? null : path[path.length - 1];
	}

	function dispatchPath(path:Array<RenderNode>, event:UiEvent):Void {
		if (path.length == 0)
			return;
		event.phase = "capture";
		for (index in 0...(path.length - 1)) {
			event.setCurrentTarget(path[index]);
			path[index].invoke(event);
			if (event.propagationStopped)
				return;
		}
		var targetIndex = path.length - 1;
		event.phase = "target";
		event.setCurrentTarget(path[targetIndex]);
		path[targetIndex].invoke(event);
		if (event.propagationStopped)
			return;
		event.phase = "bubble";
		var index = targetIndex - 1;
		while (index >= 0) {
			event.setCurrentTarget(path[index]);
			path[index].invoke(event);
			if (event.propagationStopped)
				return;
			index--;
		}
	}
}

private class PointerLocation {
	public final x:Float;
	public final y:Float;

	public function new(x:Float, y:Float) {
		this.x = x;
		this.y = y;
	}
}
