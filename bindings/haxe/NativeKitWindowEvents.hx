import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKit.Nk_event_kind;
import NativeKitEventContext;
import NativeKitEventBytes;
import NativeKitEventValue;

class NativeKitWindowEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> {
		return switch c.kind {
			case Nk_event_kind.NK_EVENT_WINDOW_CLOSE: WindowClose(c.source);
			case Nk_event_kind.NK_EVENT_WINDOW_RESIZE:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_resize_event = c.data; WindowResize(c.source, v.get_width(), v.get_height());
			case Nk_event_kind.NK_EVENT_WINDOW_MOVE:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_move_event = c.data; WindowMove(c.source, v.get_x(), v.get_y());
			case Nk_event_kind.NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_framebuffer_resize_event = c.data; WindowFramebufferResize(c.source, v.get_width(), v.get_height());
			case Nk_event_kind.NK_EVENT_WINDOW_SCALE_CHANGED:
				NativeKitEventBytes.requireSize(c.data, 4); var v:nk_window_scale_event = c.data; WindowScaleChanged(c.source, v.get_scale());
			case Nk_event_kind.NK_EVENT_WINDOW_STATE_CHANGED:
				NativeKitEventBytes.requireSize(c.data, 24); var v:nk_window_state = c.data; WindowStateChanged(c.source, v.get_flags());
			case Nk_event_kind.NK_EVENT_SURFACE_READY: SurfaceReady(c.source);
			case Nk_event_kind.NK_EVENT_SURFACE_RESIZE:
				NativeKitEventBytes.requireSize(c.data, 16); var v:nk_surface_resize_event = c.data; SurfaceResize(c.source, v.get_width(), v.get_height(), v.get_framebuffer_width(), v.get_framebuffer_height());
			case Nk_event_kind.NK_EVENT_SURFACE_LOST: SurfaceLost(c.source);
			default: null;
		}
	}
}
