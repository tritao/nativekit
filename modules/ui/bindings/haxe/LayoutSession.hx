import NativeKit;
import NativeKitUI;
import haxe.io.Bytes;

/** Batched bridge from a Haxe-owned semantic tree to NativeKit layout/rendering. */
class LayoutSession {
	var value:nkui_layout_session;
	var disposed:Bool;
	final transaction:LayoutTransaction;
	final events:Array<LayoutEvent>;

	private function new(value:nkui_layout_session) {
		this.value = value;
		disposed = false;
		transaction = new LayoutTransaction();
		events = [];
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

	/** Submits one tree and returns the semantic events produced for that frame. */
	public function submit(root:LayoutNode, frame:LayoutFrame):Array<LayoutEvent> {
		ensureLive();
		if (frame == null)
			throw "Layout session frame cannot be null";
		var transactionBytes:Bytes = transaction.encodeInto(root);
		var nativeFrame = frame.nativeValue();
		UiResult.check(NativeKitUI.nkui_layout_session_submit_slice(value, transactionBytes, 0,
			transaction.byteLength(), nativeFrame),
			"layoutSession.submit");
		var count = NativeKitUI.nkui_layout_session_get_event_count(value);
		UiResult.check(count.status, "layoutSession.eventCount");
		events.resize(0);
		for (index in 0...count.out_count) {
			var event = NativeKitUI.nkui_layout_session_get_event(value, index);
			UiResult.check(event.status, "layoutSession.event");
			events.push(new LayoutEvent(event.out_event.get_kind(), event.out_event.get_node_id()));
		}
		return events;
	}

	/** Returns the resolved bounds of a node after the latest submission. */
	public function item(node:LayoutNode):Rect {
		ensureLive();
		if (node == null)
			throw "Layout item node cannot be null";
		var nativeItem = new nkui_layout_item();
		nativeItem.set_struct_size(nkui_layout_item.size());
		var result = NativeKitUI.nkui_layout_session_get_item(value, node.id, nativeItem);
		UiResult.check(result.status, 'layoutSession.item(${node.id})');
		return new Rect(result.out_item.get_x(), result.out_item.get_y(),
			result.out_item.get_width(), result.out_item.get_height());
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

/** Semantic event emitted by a submitted layout frame. */
class LayoutEvent {
	public final kind:Int;
	public final nodeId:Int;

	public function new(kind:Int, nodeId:Int) {
		this.kind = kind;
		this.nodeId = nodeId;
	}

	public function isButtonActivated():Bool
		return kind == NativeKitUIConstants.NKUI_LAYOUT_EVENT_BUTTON_ACTIVATED;
}
