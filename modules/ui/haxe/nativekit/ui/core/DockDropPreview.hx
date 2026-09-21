package nativekit.ui.core;

/** Current target and zone preview while a dock tab is being dragged. */
class DockDropPreview {
	public final sourcePanelId:String;
	public final targetPanelId:String;
	public final zone:DockDropZone;

	public function new(sourcePanelId:String, targetPanelId:String, zone:DockDropZone) {
		if (sourcePanelId == null || targetPanelId == null || zone == null)
			throw "Dock previews require source, target, and zone";
		this.sourcePanelId = sourcePanelId;
		this.targetPanelId = targetPanelId;
		this.zone = zone;
	}

	public function same(other:Null<DockDropPreview>):Bool
		return other != null && sourcePanelId == other.sourcePanelId &&
			targetPanelId == other.targetPanelId && zone == other.zone;
}
