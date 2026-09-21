package nativekit.ui.core;

import Canvas;
import Rect;

/** Renderable content contract consumed by the shared GPU viewport node. */
interface ViewportContent {
	function width():Float;
	function height():Float;
	function revision():Int;
	function paint(canvas:Canvas, destination:Rect):Void;
}
