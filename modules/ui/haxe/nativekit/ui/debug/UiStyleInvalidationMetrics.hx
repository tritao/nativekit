package nativekit.ui.debug;

import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiDirtyFlag;
import nativekit.ui.style.StyleDiff;

/** Per-submit style invalidation counts, compared by stable render-node ID. */
class UiStyleInvalidationMetrics {
	public final styleChangedNodes:Int;
	public final styleUnchangedNodes:Int;
	public final invalidationFlags:Int;
	public final layoutInvalidatedNodes:Int;
	public final textLayoutInvalidatedNodes:Int;
	public final paintInvalidatedNodes:Int;
	public final compositeInvalidatedNodes:Int;
	public final semanticsInvalidatedNodes:Int;
	public final hitGeometryInvalidatedNodes:Int;

	function new(styleChangedNodes:Int, styleUnchangedNodes:Int, invalidationFlags:Int,
			layoutInvalidatedNodes:Int, textLayoutInvalidatedNodes:Int,
			paintInvalidatedNodes:Int, compositeInvalidatedNodes:Int,
			semanticsInvalidatedNodes:Int, hitGeometryInvalidatedNodes:Int) {
		this.styleChangedNodes = styleChangedNodes;
		this.styleUnchangedNodes = styleUnchangedNodes;
		this.invalidationFlags = invalidationFlags;
		this.layoutInvalidatedNodes = layoutInvalidatedNodes;
		this.textLayoutInvalidatedNodes = textLayoutInvalidatedNodes;
		this.paintInvalidatedNodes = paintInvalidatedNodes;
		this.compositeInvalidatedNodes = compositeInvalidatedNodes;
		this.semanticsInvalidatedNodes = semanticsInvalidatedNodes;
		this.hitGeometryInvalidatedNodes = hitGeometryInvalidatedNodes;
	}

	/** Compares current nodes against the prior submitted tree without touching layout. */
	public static function compare(previous:Null<RenderNode>, current:Null<RenderNode>):UiStyleInvalidationMetrics {
		var previousById = new Map<Int, RenderNode>();
		if (previous != null)
			previous.walk(function(node) previousById.set(node.id.value, node));

		var styleChangedNodes = 0;
		var styleUnchangedNodes = 0;
		var invalidationFlags = 0;
		var layoutInvalidatedNodes = 0;
		var textLayoutInvalidatedNodes = 0;
		var paintInvalidatedNodes = 0;
		var compositeInvalidatedNodes = 0;
		var semanticsInvalidatedNodes = 0;
		var hitGeometryInvalidatedNodes = 0;
		if (current != null)
			current.walk(function(node) {
				var prior = previousById.get(node.id.value);
				var diff = StyleDiff.compare(prior == null ? null : prior.computedStyle, node.computedStyle);
				if (!diff.changed) {
					styleUnchangedNodes++;
					return;
				}
				styleChangedNodes++;
				var flags = UiDirtyFlag.NeedsStyle | UiDirtyFlag.fromStyleImpact(diff.impact);
				invalidationFlags |= flags;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsLayout)) layoutInvalidatedNodes++;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsTextLayout)) textLayoutInvalidatedNodes++;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsPaint)) paintInvalidatedNodes++;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsComposite)) compositeInvalidatedNodes++;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsSemantics)) semanticsInvalidatedNodes++;
				if (UiDirtyFlag.contains(flags, UiDirtyFlag.NeedsHitGeometry)) hitGeometryInvalidatedNodes++;
			});

		return new UiStyleInvalidationMetrics(styleChangedNodes, styleUnchangedNodes, invalidationFlags,
			layoutInvalidatedNodes, textLayoutInvalidatedNodes, paintInvalidatedNodes,
			compositeInvalidatedNodes, semanticsInvalidatedNodes, hitGeometryInvalidatedNodes);
	}
}
