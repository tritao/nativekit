package nativekit.ui.core;

/** Persistent, view-independent description of one editor workspace region. */
enum DockNode {
	Empty;
	Panel(panelId:String);
	Tabs(panelIds:Array<String>, activePanelId:String);
	Split(axis:DockSplitAxis, ratio:Float, first:DockNode, second:DockNode);
}
