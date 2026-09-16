package nativekit.ui.widgets;

import LayoutStyle;

/** Policies and controlled state for a SplitView's resizable pane. */
class SplitViewOptions {
	public var orientation:SplitOrientation;
	public var resizableSide:SplitSide;
	public var extent:Float;
	public var minimumExtent:Float;
	public var maximumExtent:Float;
	public var collapsed:Bool;
	public var dividerExtent:Float;
	public var style:Null<LayoutStyle>;
	public var dividerStyle:Null<LayoutStyle>;
	public var onResize:Null<Float->Void>;
	public var onCollapsedChanged:Null<Bool->Void>;

	public function new() {
		orientation = SplitOrientation.Horizontal;
		resizableSide = SplitSide.Trailing;
		extent = 280.0;
		minimumExtent = 160.0;
		maximumExtent = 480.0;
		collapsed = false;
		dividerExtent = 8.0;
		style = null;
		dividerStyle = null;
		onResize = null;
		onCollapsedChanged = null;
	}
}
