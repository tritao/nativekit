import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKitEventContext;
import NativeKitEventValue;

class NativeKitWindowEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> {
		return switch c.kind {
			case NativeKitConstants.NK_EVENT_WINDOW_CLOSE: WindowClose(c.source);
			case NativeKitConstants.NK_EVENT_WINDOW_RESIZE:
				size(c, 8); var v:nk_window_resize_event = c.data; WindowResize(c.source, v.get_width(), v.get_height());
			case NativeKitConstants.NK_EVENT_WINDOW_MOVE:
				size(c, 8); var v:nk_window_move_event = c.data; WindowMove(c.source, v.get_x(), v.get_y());
			case NativeKitConstants.NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE:
				size(c, 8); var v:nk_window_framebuffer_resize_event = c.data; WindowFramebufferResize(c.source, v.get_width(), v.get_height());
			case NativeKitConstants.NK_EVENT_WINDOW_SCALE_CHANGED:
				size(c, 4); var v:nk_window_scale_event = c.data; WindowScaleChanged(c.source, v.get_scale());
			case NativeKitConstants.NK_EVENT_WINDOW_STATE_CHANGED:
				size(c, 24); var v:nk_window_state = c.data; WindowStateChanged(c.source, v.get_flags());
			case NativeKitConstants.NK_EVENT_SURFACE_READY: SurfaceReady(c.source);
			case NativeKitConstants.NK_EVENT_SURFACE_RESIZE:
				size(c, 16); var v:nk_surface_resize_event = c.data; SurfaceResize(c.source, v.get_width(), v.get_height(), v.get_framebuffer_width(), v.get_framebuffer_height());
			case NativeKitConstants.NK_EVENT_SURFACE_LOST: SurfaceLost(c.source);
			default: null;
		}
	}

	static function size(c:NativeKitEventContext, expected:Int):Void
		if (c.data.length != expected) throw "NativeKit window event payload has an invalid size";
}
