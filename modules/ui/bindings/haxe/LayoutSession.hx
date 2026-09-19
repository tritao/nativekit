import NativeKit;
import NativeKitUI;
import NativeKitUI.nkui_layout_measure_callbackCallback;
import haxe.io.Bytes;

/** Batched bridge from a Haxe-owned render tree to NativeKit layout/rendering. */
class LayoutSession {
	var value:nkui_layout_session;
	var disposed:Bool;
	final transaction:LayoutTransaction;
	final resolved:Array<ResolvedLayoutItem>;
	var measureCallback:Null<nkui_layout_measure_callbackCallback>;
	var measureFunction:Null<LayoutMeasureCallback>;
	final measureContents:Map<Int, LayoutContent>;
	final renderableContents:Map<Int, LayoutRenderableContent>;
	var hasMeasureContents:Bool;

	private function new(value:nkui_layout_session) {
		this.value = value;
		disposed = false;
		transaction = new LayoutTransaction();
		resolved = [];
		measureCallback = null;
		measureFunction = null;
		measureContents = new Map();
		renderableContents = new Map();
		hasMeasureContents = false;
	}

	public static function create():LayoutSession {
		var made = NativeKitUI.nkui_layout_session_create();
		UiResult.check(made.status, "layoutSession.create");
		return new LayoutSession(made.out_session);
	}

	/** Copies font paths/data and fallback policy into this session. */
	public function setFonts(fonts:FontCollection):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_layout_session_set_font_collection(value,
			fonts.nativeHandle()), "layoutSession.setFonts");
	}

	/**
	 * Installs a fallback intrinsic measurer for Custom nodes without an
	 * attached LayoutContent provider. The callback runs during submit and
	 * remains retained by this session until replaced or cleared.
	 */
	public function setMeasureCallback(callback:Null<LayoutMeasureCallback>):Void {
		ensureLive();
		if (measureCallback != null)
			detachMeasureCallback();
		measureFunction = callback;
		refreshMeasureCallback();
	}

	/** Submits one render tree and returns geometry for the complete resolved frame. */
	public function submit(root:LayoutNode, frame:LayoutFrame):Array<ResolvedLayoutItem> {
		ensureLive();
		if (frame == null)
			throw "Layout session frame cannot be null";
		var transactionBytes:Bytes = transaction.encodeInto(root);
		collectMeasureContents(root);
		refreshMeasureCallback();
		var nativeFrame = frame.nativeValue();
		UiResult.check(NativeKitUI.nkui_layout_session_submit_slice(value, transactionBytes, 0,
			transaction.byteLength(), nativeFrame),
			"layoutSession.submit");
		var result = NativeKitUI.nkui_layout_session_get_resolved_items(value);
		UiResult.check(result.status, "layoutSession.resolvedItems");
		var bytes:Bytes = result.out_buffer;
		var recordBytes = NativeKitUIConstants.NKUI_LAYOUT_RESOLVED_ITEM_BYTES;
		if (bytes.length % recordBytes != 0)
			throw "Native layout returned a truncated geometry snapshot";
		resolved.resize(0);
		for (index in 0...Std.int(bytes.length / recordBytes))
			resolved.push(ResolvedLayoutItem.decode(bytes, index * recordBytes));
		return resolved;
	}

	/** Returns pre-transform layout bounds of a node after the latest submission. */
	public function item(node:LayoutNode):Rect {
		ensureLive();
		if (node == null)
			throw "Layout item node cannot be null";
		for (item in resolved)
			if (item.id == node.id)
				return item.bounds();
		throw 'Layout item ${node.id} is not present in the latest resolved frame';
	}

	/** Returns cumulative intrinsic measurement and cache activity for this session. */
	public function measureStats():LayoutMeasureStats {
		ensureLive();
		var result = NativeKitUI.nkui_layout_session_get_measure_stats(value);
		UiResult.check(result.status, "layoutSession.measureStats");
		var stats = result.out_stats;
		return new LayoutMeasureStats(stats.get_requests(), stats.get_cache_hits(),
			stats.get_cache_misses(), stats.get_callback_calls(), stats.get_cache_entries(),
			stats.get_cache_capacity());
	}

	/** Removes all Haxe custom-paint display lists from this session. */
	public function clearCustomPaints():Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_layout_session_clear_custom_paints(value),
			"layoutSession.clearCustomPaints");
	}

	/** Attaches a custom display list to a Custom layout node for ordered rendering. */
	public function setCustomPaint(nodeId:Int, displayList:DisplayList):Void {
		ensureLive();
		if (nodeId <= 0 || displayList == null || displayList.isDisposed())
			throw "Custom paint requires a live display list and node ID";
		UiResult.check(NativeKitUI.nkui_layout_session_set_custom_paint(value, nodeId,
			displayList.nativeHandle()), "layoutSession.setCustomPaint");
	}

	/** Selects vector or persistent GPU-raster rendering for a custom paint plane. */
	public function setCustomPaintCachePolicy(nodeId:Int, policy:Int):Void {
		ensureLive();
		if (nodeId <= 0 || policy < 0 || policy > 2)
			throw "Custom paint cache policy is invalid";
		UiResult.check(NativeKitUI.nkui_layout_session_set_custom_paint_cache_policy(value,
			nodeId, policy), "layoutSession.setCustomPaintCachePolicy");
	}

	/** Selects vector or persistent GPU-raster rendering for a render-node subtree. */
	public function setCachePolicy(nodeId:Int, policy:Int):Void {
		ensureLive();
		if (nodeId <= 0 || policy < 0 || policy > 2)
			throw "Render subtree cache policy is invalid";
		UiResult.check(NativeKitUI.nkui_layout_session_set_cache_policy(value, nodeId, policy),
			"layoutSession.setCachePolicy");
	}

	/** Executes the last submitted tree through the existing renderer backend. */
	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		updateRenderablePaints();
		UiResult.check(NativeKitUI.nkui_layout_session_render_frame(renderer.nativeHandle(), value,
			surface.nativeHandle(), frame.nativeValue(), false), "layoutSession.render");
	}

	/** Composites the last submitted tree over the current surface contents. */
	public function renderOverlay(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		updateRenderablePaints();
		UiResult.check(NativeKitUI.nkui_layout_session_render_frame(renderer.nativeHandle(), value,
			surface.nativeHandle(), frame.nativeValue(), true), "layoutSession.renderOverlay");
	}

	public function dispose():Void {
		if (disposed)
			return;
		detachMeasureCallback();
		var status = NativeKitUI.nkui_layout_session_destroy(value);
		disposed = true;
		UiResult.check(status, "layoutSession.dispose");
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Layout session has been disposed";
	}

	function refreshMeasureCallback():Void {
		if (measureFunction == null && !hasMeasureContents) {
			detachMeasureCallback();
			return;
		}
		if (measureCallback != null)
			return;
		var nativeCallback = new nkui_layout_measure_callbackCallback(
			function(nodeId:Int, constraints:nkui_layout_measure_constraints,
				_userData:Null<hl.Abstract<"native_pointer">>) {
				var measured = measureNode(nodeId, LayoutMeasureConstraints.fromNative(constraints));
				if (measured == null)
					throw "Layout measurement callback returned null";
				return measured.nativeValue();
			});
		try {
			UiResult.check(NativeKitUI.nkui_layout_session_set_measure_callback(value, nativeCallback,
				null), "layoutSession.setMeasureCallback");
		} catch (error:Dynamic) {
			nativeCallback.close();
			throw error;
		}
		measureCallback = nativeCallback;
	}

	function measureNode(nodeId:Int, constraints:LayoutMeasureConstraints):LayoutMeasureResult {
		var content = measureContents.get(nodeId);
		if (content != null)
			return content.measure(constraints);
		if (measureFunction != null)
			return measureFunction(nodeId, constraints);
		return new LayoutMeasureResult(0.0, 0.0);
	}

	function collectMeasureContents(root:LayoutNode):Void {
		measureContents.clear();
		renderableContents.clear();
		hasMeasureContents = false;
		collectMeasureContentsFrom(root);
	}

	function collectMeasureContentsFrom(node:LayoutNode):Void {
		if (node.intrinsicContent != null) {
			if (node.visualKind != LayoutVisualKind.Custom)
				throw "Intrinsic content requires a Custom layout node";
			measureContents.set(node.id, node.intrinsicContent);
			if (Std.isOfType(node.intrinsicContent, LayoutRenderableContent))
				renderableContents.set(node.id, cast node.intrinsicContent);
			hasMeasureContents = true;
		}
		for (child in node.children)
			collectMeasureContentsFrom(child);
	}

	function updateRenderablePaints():Void {
		for (nodeId in renderableContents.keys()) {
			var content = renderableContents.get(nodeId);
			if (content == null)
				continue;
			var geometry:Null<ResolvedLayoutItem> = null;
			for (item in resolved)
				if (item.id == nodeId) {
					geometry = item;
					break;
				}
			if (geometry == null || !geometry.visible || geometry.width <= 0.0 ||
				geometry.height <= 0.0 || geometry.clipBounds.width <= 0.0 ||
				geometry.clipBounds.height <= 0.0)
				continue;
			var displayList = content.paint(geometry);
			setCustomPaint(nodeId, displayList);
		}
	}

	function detachMeasureCallback():Void {
		if (measureCallback == null)
			return;
		UiResult.check(NativeKitUI.nkui_layout_session_set_measure_callback(value, null, null),
			"layoutSession.clearMeasureCallback");
		var callback = measureCallback;
		measureCallback = null;
		callback.close();
	}
}
