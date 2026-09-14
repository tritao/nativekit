package nativekit.ui.core;

import NativeKit;

/** Capture/target/bubble dispatch with per-pointer hover and pointer capture. */
class EventDispatcher {
	var root:Null<RenderNode>;
	final focus:FocusManager;
	final hoverPaths:Map<Int, Array<RenderNode>>;
	final capturedIds:Map<Int, WidgetId>;

	public function new(focus:FocusManager) {
		this.focus = focus;
		root = null;
		hoverPaths = new Map();
		capturedIds = new Map();
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
			capturedIds.remove(pointerId);
	}

	public function pointerMove(x:Float, y:Float, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		var path = HitTest.path(root, x, y);
		updateHover(pointerId, path, x, y);
		var targetPath = capturedPath(pointerId);
		if (targetPath.length == 0)
			targetPath = path;
		if (targetPath.length > 0)
			dispatchPath(targetPath, new UiEvent(UiEventKind.PointerMove,
				targetPath[targetPath.length - 1].id, x, y, 0.0, 0.0, 0, 0,
				modifiers, null, data, 0, pointerId));
	}

	public function pointerDown(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null, timestamp:Float = -1.0):Void {
		var path = HitTest.path(root, x, y);
		if (path.length == 0)
			return;
		var target = path[path.length - 1];
		capturedIds.set(pointerId, target.id);
		var index = path.length - 1;
		while (index >= 0) {
			if (path[index].focusable && path[index].enabled && changeFocus(path[index].id))
				break;
			index--;
		}
		dispatchPath(path, new UiEvent(UiEventKind.PointerDown, target.id, x, y,
			0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId,
			timestamp < 0.0 ? NativeKit.nk_time_seconds() : timestamp));
	}

	public function pointerUp(x:Float, y:Float, button:Int, modifiers:Int = 0,
			pointerId:Int = 0, data:Dynamic = null):Void {
		var path = capturedPath(pointerId);
		var pressed = capturedIds.get(pointerId);
		capturedIds.remove(pointerId);
		if (path.length == 0)
			path = HitTest.path(root, x, y);
		if (path.length == 0)
			return;
		var target = path[path.length - 1];
		dispatchPath(path, new UiEvent(UiEventKind.PointerUp, target.id, x, y,
			0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId));
		var releasePath = HitTest.path(root, x, y);
		if (pressed != null && releasePath.length > 0 &&
			releasePath[releasePath.length - 1].id.equals(pressed)) {
			dispatchPath(releasePath, new UiEvent(UiEventKind.Click, pressed, x, y,
				0.0, 0.0, button, 0, modifiers, null, data, 0, pointerId));
		}
	}

	public function pointerCancel(pointerId:Int, x:Float, y:Float,
			modifiers:Int = 0, data:Dynamic = null):Void {
		var path = capturedPath(pointerId);
		capturedIds.remove(pointerId);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(UiEventKind.PointerCancel,
				path[path.length - 1].id, x, y, 0.0, 0.0, 0, 0, modifiers,
				null, data, 0, pointerId));
		updateHover(pointerId, [], x, y);
	}

	public function scroll(x:Float, y:Float, deltaX:Float, deltaY:Float,
			modifiers:Int = 0):Void {
		var path = HitTest.path(root, x, y);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(UiEventKind.Scroll, path[path.length - 1].id,
				x, y, deltaX, deltaY, 0, 0, modifiers));
	}

	public function key(kind:String, key:Int, modifiers:Int = 0, scancode:Int = 0):Void {
		var node = focus.focusedNode();
		if (node == null)
			return;
		var path = HitTest.pathTo(node);
		if (path.length == 0)
			return;
		var event = new UiEvent(kind, node.id, 0.0, 0.0, 0.0, 0.0, 0, key,
			modifiers, null, null, scancode);
		dispatchPath(path, event);
		if (event.defaultPrevented || kind != UiEventKind.KeyDown)
			return;
		if (key == UiKey.Tab)
			moveFocus((modifiers & UiModifier.Shift) != 0);
		else if (key == UiKey.Enter || key == UiKey.Space)
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
		capturedIds.remove(pointerId);
	}

	public function cancelPointers():Void {
		var active:Array<Int> = [];
		for (pointerId in capturedIds.keys())
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
		dispatchDirect(id, kind);

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
			dispatchPath(previous.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverLeave, previous[index].id, x, y,
					0.0, 0.0, 0, 0, 0, null, null, 0, pointerId));
			index--;
		}
		for (index in common...path.length)
			dispatchPath(path.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverEnter, path[index].id, x, y,
					0.0, 0.0, 0, 0, 0, null, null, 0, pointerId));
		hoverPaths.set(pointerId, path);
	}

	function capturedPath(pointerId:Int):Array<RenderNode> {
		var id = capturedIds.get(pointerId);
		if (id == null || root == null)
			return [];
		return HitTest.pathTo(root.find(id));
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
		var node = root.find(id);
		if (node == null)
			return;
		var event = new UiEvent(kind, id);
		event.currentTarget = id;
		event.phase = "target";
		node.invoke(event);
	}

	function dispatchPath(path:Array<RenderNode>, event:UiEvent):Void {
		if (path.length == 0)
			return;
		event.phase = "capture";
		for (index in 0...(path.length - 1)) {
			event.currentTarget = path[index].id;
			path[index].invoke(event);
			if (event.propagationStopped)
				return;
		}
		var targetIndex = path.length - 1;
		event.phase = "target";
		event.currentTarget = path[targetIndex].id;
		path[targetIndex].invoke(event);
		if (event.propagationStopped)
			return;
		event.phase = "bubble";
		var index = targetIndex - 1;
		while (index >= 0) {
			event.currentTarget = path[index].id;
			path[index].invoke(event);
			if (event.propagationStopped)
				return;
			index--;
		}
	}
}
