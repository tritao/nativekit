import NativeKit;
import NativeKit.NativeKitConstants;
import NativeKit.NkEventKind;
import NativeKitEventContext;
import NativeKitEventBytes;
import NativeKitEventValue;

class NativeKitWindowEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> {
		return switch c.kind {
			case NkEventKind.WindowClose: WindowClose(c.source);
			case NkEventKind.WindowResize:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_resize_event = c.data; WindowResize(c.source, v.get_width(), v.get_height());
			case NkEventKind.WindowMove:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_move_event = c.data; WindowMove(c.source, v.get_x(), v.get_y());
			case NkEventKind.WindowFramebufferResize:
				NativeKitEventBytes.requireSize(c.data, 8); var v:nk_window_framebuffer_resize_event = c.data; WindowFramebufferResize(c.source, v.get_width(), v.get_height());
			case NkEventKind.WindowScaleChanged:
				NativeKitEventBytes.requireSize(c.data, 4); var v:nk_window_scale_event = c.data; WindowScaleChanged(c.source, v.get_scale());
			case NkEventKind.WindowStateChanged:
				NativeKitEventBytes.requireSize(c.data, 24); var v:nk_window_state = c.data; WindowStateChanged(c.source, v.get_flags());
			case NkEventKind.SurfaceReady: SurfaceReady(c.source);
			case NkEventKind.SurfaceResize:
				NativeKitEventBytes.requireSize(c.data, 16); var v:nk_surface_resize_event = c.data; SurfaceResize(c.source, v.get_width(), v.get_height(), v.get_framebuffer_width(), v.get_framebuffer_height());
			case NkEventKind.SurfaceLost: SurfaceLost(c.source);
			default: null;
		}
	}
}
