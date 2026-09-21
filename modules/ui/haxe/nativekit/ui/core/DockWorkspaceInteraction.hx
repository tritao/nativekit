package nativekit.ui.core;

/** Shared tab-drag controller that translates pointer geometry into model docks. */
class DockWorkspaceInteraction {
	public final model:DockWorkspaceModel;
	public var draggingPanelId(default, null):Null<String>;
	public var preview(default, null):Null<DockDropPreview>;
	public var revision(default, null):Int;
	final targets:Array<DockDropTarget>;
	final tabTargets:Array<DockTabDropTarget>;
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
		tabTargets = [];
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
		tabTargets.splice(0, tabTargets.length);
	}

	/** Registers one tab header as a precise before/after reorder target. */
	public function registerTabTarget(target:DockTabDropTarget):Void {
		if (target == null)
			return;
		for (existing in tabTargets)
			if (existing.targetPanelId == target.targetPanelId && existing.node == target.node)
				return;
		tabTargets.push(target);
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
		if (selected == null)
			return false;
		// A drag released on its own header is still a valid activation. This
		// keeps pointer-drag capture from swallowing the tab's normal selection.
		return selected.sourcePanelId == selected.targetPanelId
			? model.activate(selected.sourcePanelId)
			: model.dock(selected.sourcePanelId, selected.targetPanelId, selected.zone);
	}

	public function cancelTabDrag(panelId:String, nextPointerId:Int):Bool {
		if (draggingPanelId != panelId || pointerId != nextPointerId)
			return false;
		clearDrag();
		return true;
	}

	function updatePreview(x:Float, y:Float):Void {
		var next:Null<DockDropPreview> = null;
		if (draggingPanelId != null) {
			// Header targets win over broad panel targets so a center drop can
			// reorder an existing tab group instead of merely appending a tab.
			for (index in 0...tabTargets.length) {
				var tabTarget = tabTargets[tabTargets.length - index - 1];
				var tabZone = tabTarget.zoneAt(x, y);
				if (tabZone != null) {
					next = new DockDropPreview(draggingPanelId, tabTarget.targetPanelId, tabZone);
					break;
				}
			}
		}
		if (next == null && draggingPanelId != null)
			for (index in 0...targets.length) {
				var target = targets[targets.length - index - 1];
				var zone = target.zoneAt(x, y);
				if (zone != null) {
					next = new DockDropPreview(draggingPanelId, target.targetPanelId, zone);
					break;
				}
			}
		if (preview == null && next != null || preview != null && !preview.same(next)) {
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
