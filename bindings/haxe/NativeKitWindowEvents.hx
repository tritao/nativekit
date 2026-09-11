import NativeKit;
import NativeKit.EventKind;
import NativeKit.Event;
import NativeKit.SurfaceResizeEvent;
import NativeKit.WindowFramebufferResizeEvent;
import NativeKit.WindowMoveEvent;
import NativeKit.WindowResizeEvent;
import NativeKit.WindowScaleEvent;
import NativeKit.WindowState;
import NativeKitEventContext;
import NativeKitEventBytes;
import NativeKitEventValue;

class NativeKitWindowEvents {
	public static function decode(c:NativeKitEventContext):Null<NativeKitEventValue> {
		return switch c.kind {
			case EventKind.WindowClose: WindowClose(c.source);
			case EventKind.WindowResize:
				NativeKitEventBytes.requireSize(c.data, 8); var v:WindowResizeEvent = c.data; WindowResize(c.source, v.get_width(), v.get_height());
			case EventKind.WindowMove:
				NativeKitEventBytes.requireSize(c.data, 8); var v:WindowMoveEvent = c.data; WindowMove(c.source, v.get_x(), v.get_y());
			case EventKind.WindowFramebufferResize:
				NativeKitEventBytes.requireSize(c.data, 8); var v:WindowFramebufferResizeEvent = c.data; WindowFramebufferResize(c.source, v.get_width(), v.get_height());
			case EventKind.WindowScaleChanged:
				NativeKitEventBytes.requireSize(c.data, 4); var v:WindowScaleEvent = c.data; WindowScaleChanged(c.source, v.get_scale());
			case EventKind.WindowStateChanged:
				NativeKitEventBytes.requireSize(c.data, 24); var v:WindowState = c.data; WindowStateChanged(c.source, v.get_flags());
			case EventKind.SurfaceReady: SurfaceReady(c.source);
			case EventKind.SurfaceResize:
				NativeKitEventBytes.requireSize(c.data, 16); var v:SurfaceResizeEvent = c.data; SurfaceResize(c.source, v.get_width(), v.get_height(), v.get_framebuffer_width(), v.get_framebuffer_height());
			case EventKind.SurfaceLost: SurfaceLost(c.source);
			default: null;
		}
	}
}
