package nativekit.ui.core;

/** Capture/target/bubble dispatch with hover tracking and pointer capture. */
class EventDispatcher {
	var root:Null<RenderNode>;
	final focus:FocusManager;
	var hoverPath:Array<RenderNode>;
	var capturedId:Null<WidgetId>;

	public function new(focus:FocusManager) {
		this.focus = focus;
		root = null;
		hoverPath = [];
		capturedId = null;
	}

	public function setRoot(root:Null<RenderNode>):Void {
		this.root = root;
		if (capturedId != null && (root == null || root.find(capturedId) == null))
			capturedId = null;
		hoverPath = [];
	}

	public function pointerMove(x:Float, y:Float, modifiers:Int = 0):Void {
		var path = HitTest.path(root, x, y);
		updateHover(path, x, y);
		var targetPath = capturedPath();
		if (targetPath.length == 0)
			targetPath = path;
		if (targetPath.length > 0)
			dispatchPath(targetPath, new UiEvent(UiEventKind.PointerMove,
				targetPath[targetPath.length - 1].id, x, y, 0.0, 0.0, 0, 0, modifiers));
	}

	public function pointerDown(x:Float, y:Float, button:Int, modifiers:Int = 0):Void {
		var path = HitTest.path(root, x, y);
		if (path.length == 0)
			return;
		var target = path[path.length - 1];
		capturedId = target.id;
		var index = path.length - 1;
		while (index >= 0) {
			if (path[index].focusable && path[index].enabled) {
				changeFocus(path[index].id);
				break;
			}
			index--;
		}
		dispatchPath(path, new UiEvent(UiEventKind.PointerDown, target.id, x, y,
			0.0, 0.0, button, 0, modifiers));
	}

	public function pointerUp(x:Float, y:Float, button:Int, modifiers:Int = 0):Void {
		var path = capturedPath();
		var pressed = capturedId;
		capturedId = null;
		if (path.length == 0)
			path = HitTest.path(root, x, y);
		if (path.length == 0)
			return;
		var target = path[path.length - 1];
		dispatchPath(path, new UiEvent(UiEventKind.PointerUp, target.id, x, y,
			0.0, 0.0, button, 0, modifiers));
		var releasePath = HitTest.path(root, x, y);
		if (pressed != null && releasePath.length > 0 &&
			releasePath[releasePath.length - 1].id.equals(pressed)) {
			dispatchPath(releasePath, new UiEvent(UiEventKind.Click, pressed, x, y,
				0.0, 0.0, button, 0, modifiers));
		}
	}

	public function scroll(x:Float, y:Float, deltaX:Float, deltaY:Float, modifiers:Int = 0):Void {
		var path = HitTest.path(root, x, y);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(UiEventKind.Scroll, path[path.length - 1].id,
				x, y, deltaX, deltaY, 0, 0, modifiers));
	}

	public function key(kind:String, key:Int, modifiers:Int = 0):Void {
		var node = focus.focusedNode();
		if (node == null)
			return;
		var path = HitTest.pathTo(node);
		if (path.length > 0)
			dispatchPath(path, new UiEvent(kind, node.id, 0.0, 0.0,
				0.0, 0.0, 0, key, modifiers));
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

	public function clearPointer():Void {
		updateHover([], 0.0, 0.0);
		capturedId = null;
	}

	public function focusEvent(id:WidgetId, kind:String):Void
		dispatchDirect(id, kind);

	function updateHover(path:Array<RenderNode>, x:Float, y:Float):Void {
		var common = 0;
		while (common < hoverPath.length && common < path.length &&
			hoverPath[common].id.equals(path[common].id))
			common++;
		var index = hoverPath.length - 1;
		while (index >= common) {
			dispatchPath(hoverPath.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverLeave, hoverPath[index].id, x, y));
			index--;
		}
		for (index in common...path.length)
			dispatchPath(path.slice(0, index + 1),
				new UiEvent(UiEventKind.HoverEnter, path[index].id, x, y));
		hoverPath = path;
	}

	function capturedPath():Array<RenderNode> {
		if (capturedId == null || root == null)
			return [];
		return HitTest.pathTo(root.find(capturedId));
	}

	function changeFocus(id:WidgetId):Void {
		var previous = focus.focusedId;
		if (previous != null && previous.equals(id))
			return;
		if (!focus.focus(id))
			return;
		if (previous != null)
			dispatchDirect(previous, UiEventKind.Blur);
		dispatchDirect(id, UiEventKind.Focus);
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
