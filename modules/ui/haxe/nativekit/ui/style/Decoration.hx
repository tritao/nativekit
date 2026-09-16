package nativekit.ui.style;

import Canvas;
import ResolvedLayoutItem;

/** Reusable visual effect painted after layout resolves geometry. */
interface Decoration {
	function paint(canvas:Canvas, geometry:ResolvedLayoutItem, style:ComputedStyle):Void;
}
