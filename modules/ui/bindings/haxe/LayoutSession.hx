import NativeKit;
import NativeKitUI;
import haxe.io.Bytes;

/** Batched bridge from a Haxe-owned render tree to NativeKit layout/rendering. */
class LayoutSession {
	var value:nkui_layout_session;
	var disposed:Bool;
	final transaction:LayoutTransaction;
	final resolved:Array<ResolvedLayoutItem>;

	private function new(value:nkui_layout_session) {
		this.value = value;
		disposed = false;
		transaction = new LayoutTransaction();
		resolved = [];
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

	/** Submits one render tree and returns geometry for the complete resolved frame. */
	public function submit(root:LayoutNode, frame:LayoutFrame):Array<ResolvedLayoutItem> {
		ensureLive();
		if (frame == null)
			throw "Layout session frame cannot be null";
		var transactionBytes:Bytes = transaction.encodeInto(root);
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

	/** Executes the last submitted tree through the existing renderer backend. */
	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_layout_session_render_frame(renderer.nativeHandle(), value,
			surface.nativeHandle(), frame.nativeValue(), 0), "layoutSession.render");
	}

	/** Composites the last submitted tree over the current surface contents. */
	public function renderOverlay(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_layout_session_render_frame(renderer.nativeHandle(), value,
			surface.nativeHandle(), frame.nativeValue(), 1), "layoutSession.renderOverlay");
	}

	public function dispose():Void {
		if (disposed)
			return;
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
}
