import NativeKit;
import NativeKitUI;
import haxe.io.Bytes;

/** Batched bridge from a Haxe-owned semantic tree to NativeKit layout/rendering. */
class LayoutSession {
	var value:nkui_layout_session;
	var disposed:Bool;

	private function new(value:nkui_layout_session) {
		this.value = value;
		disposed = false;
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
	public function submit(root:LayoutNode, width:Float, height:Float, pointerX:Float = 0.0,
			pointerY:Float = 0.0, pointerDown:Bool = false, deltaSeconds:Float = 0.0):Array<LayoutEvent> {
		ensureLive();
		var transaction:Bytes = LayoutTransaction.encode(root);
		UiResult.check(NativeKitUI.nkui_layout_session_submit(value, transaction, width, height,
			pointerX, pointerY, pointerDown ? 1 : 0, deltaSeconds), "layoutSession.submit");
		var count = NativeKitUI.nkui_layout_session_get_event_count(value);
		UiResult.check(count.status, "layoutSession.eventCount");
		var events:Array<LayoutEvent> = [];
		for (index in 0...count.out_count) {
			var event = NativeKitUI.nkui_layout_session_get_event(value, index);
			UiResult.check(event.status, "layoutSession.event");
			events.push(new LayoutEvent(event.out_event.get_kind(), event.out_event.get_node_id()));
		}
		return events;
	}

	/** Executes the last submitted tree through the existing renderer backend. */
	public function render(renderer:Renderer, surface:Surface, frame:FrameInfo):Void {
		ensureLive();
		UiResult.check(NativeKitUI.nkui_layout_session_render_frame(renderer.nativeHandle(), value,
			surface.nativeHandle(), frame.nativeValue()), "layoutSession.render");
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
