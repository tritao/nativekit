package nativekit.ui.core;

/** Shared tab-drag controller that translates pointer geometry into model docks. */
class DockWorkspaceInteraction {
	public final model:DockWorkspaceModel;
	public var draggingPanelId(default, null):Null<String>;
	public var preview(default, null):Null<DockDropPreview>;
	public var revision(default, null):Int;
	final targets:Array<DockDropTarget>;
	final listeners:Array<Void->Void>;
	var pointerId:Int;

	public function new(model:DockWorkspaceModel) {
		if (model == null)
			throw "Dock interaction requires a workspace model";
		this.model = model;
		draggingPanelId = null;
		preview = null;
		revision = 0;
		targets = [];
		listeners = [];
		pointerId = -1;
	}

	public function listen(callback:Void->Void):Void {
		if (callback != null && !listeners.contains(callback))
			listeners.push(callback);
	}

	/** Replaces render targets after each workspace rebuild. */
	public function beginFrame():Void {
		targets.splice(0, targets.length);
	}

	public function registerTarget(target:DockDropTarget):Void {
		if (target == null)
			return;
		for (existing in targets)
			if (existing.node == target.node)
				return;
		targets.push(target);
	}

	public function beginTabDrag(panelId:String, nextPointerId:Int, x:Float, y:Float):Bool {
		if (draggingPanelId != null || panelId == null || !model.isOpen(panelId))
			return false;
		draggingPanelId = panelId;
		pointerId = nextPointerId;
		updatePreview(x, y);
		return true;
	}

	public function moveTabDrag(panelId:String, nextPointerId:Int, x:Float, y:Float):Bool {
		if (draggingPanelId != panelId || pointerId != nextPointerId)
			return false;
		updatePreview(x, y);
		return true;
	}

	public function endTabDrag(panelId:String, nextPointerId:Int, x:Float, y:Float):Bool {
		if (draggingPanelId != panelId || pointerId != nextPointerId)
			return false;
		updatePreview(x, y);
		var selected = preview;
		clearDrag();
		return selected != null && model.dock(selected.sourcePanelId, selected.targetPanelId, selected.zone);
	}

	public function cancelTabDrag(panelId:String, nextPointerId:Int):Bool {
		if (draggingPanelId != panelId || pointerId != nextPointerId)
			return false;
		clearDrag();
		return true;
	}

	function updatePreview(x:Float, y:Float):Void {
		var next:Null<DockDropPreview> = null;
		if (draggingPanelId != null)
			for (index in 0...targets.length) {
				var target = targets[targets.length - index - 1];
				var zone = target.zoneAt(x, y);
				if (zone != null) {
					next = new DockDropPreview(draggingPanelId, target.targetPanelId, zone);
					break;
				}
			}
		if (preview == null && next == null || preview != null && !preview.same(next)) {
			preview = next;
			touch();
		}
	}

	function clearDrag():Void {
		draggingPanelId = null;
		pointerId = -1;
		preview = null;
		touch();
	}

	function touch():Void {
		revision++;
		var callbacks = listeners.copy();
		for (callback in callbacks)
			callback();
	}
}
