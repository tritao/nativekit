package nativekit.ui.widgets;

import LayoutAxis;
import nativekit.ui.core.View;

/** A positioned child layer submitted to a Stack. */
class StackChild {
	public final key:String;
	public final view:View;
	public final x:Float;
	public final y:Float;
	public final zIndex:Int;
	public final width:Null<LayoutAxis>;
	public final height:Null<LayoutAxis>;
	public final clipToParent:Bool;

	public function new(key:String, view:View, x:Float = 0.0, y:Float = 0.0,
			zIndex:Int = 0, ?width:LayoutAxis, ?height:LayoutAxis,
			clipToParent:Bool = true) {
		if (key == null || key.length == 0 || view == null || !finite(x) || !finite(y) ||
			zIndex < -32768 || zIndex > 32767)
			throw "Stack children require a key, finite position, view, and valid z-index";
		this.key = key;
		this.view = view;
		this.x = x;
		this.y = y;
		this.zIndex = zIndex;
		this.width = width;
		this.height = height;
		this.clipToParent = clipToParent;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
