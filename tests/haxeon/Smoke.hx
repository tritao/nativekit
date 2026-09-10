import NativeKit;
import NativeKitEvent;

class Smoke {
	static function main():Int {
		if (NativeKit.nk_api_version() != 1)
			return 1;

		var options = new nk_init_options();
		options.set_struct_size(16);
		options.set_api_version(1);
		options.set_event_queue_capacity(32);
		if (NativeKit.nk_init(options) != 0)
			return 2;

		var empty = NativeKitEvent.poll();
		var eventOk = switch empty.take() {
			case None: empty.isReleased() && !empty.release();
			case _: false;
		};
		try {
			empty.payload();
			eventOk = false;
		} catch (_:Dynamic) {}
		try {
			NativeKitEvent.decodeClipboardFiles(haxe.io.Bytes.alloc(4), 0);
			eventOk = false;
		} catch (_:Dynamic) {}
		var payloadOk = true;
		if (NativeKit.nk_clipboard_set_text("nativekit ffi") == 0) {
			var request = NativeKit.nk_clipboard_read_text();
			payloadOk = request.status == 0;
			var completed = false;
			for (_ in 0...1000) {
				var next = NativeKitEvent.poll();
				if (next.kind == 400) {
					payloadOk = switch next.take() {
						case ClipboardText(completedRequest, completedResult, text):
							completedResult == 0 && text == "nativekit ffi";
						case _: false;
					};
					completed = true;
					break;
				}
				next.release();
			}
			payloadOk = payloadOk && completed;
		}

		var windowOptions = new nk_window_options();
		windowOptions.set_struct_size(40);
		windowOptions.set_flags(2);
		windowOptions.set_width(320);
		windowOptions.set_height(200);
		var created = NativeKit.nk_window_create(windowOptions);
		var windowOk = created.status == -4;
		if (created.status == 0)
			windowOk = created.out_window != 0 && NativeKit.nk_window_destroy(created.out_window) == 0;
		var primary = NativeKit.nk_monitor_get_primary();
		var monitorOk = primary.status == -4;
		if (primary.status == 0) {
			var name = NativeKit.nk_monitor_get_name(primary.out_monitor);
			monitorOk = primary.out_monitor != 0 && name.status == 0 && name.buffer != null;
		}

		var diagnosticOk = NativeKit.nk_window_destroy(0) == -3 && NativeKit.nk_last_error() != null;
		NativeKit.nk_shutdown();
		if (!eventOk)
			return 3;
		if (!windowOk)
			return 4;
		if (!diagnosticOk)
			return 5;
		if (!monitorOk)
			return 6;
		if (!payloadOk)
			return 7;
		return 42;
	}
}
