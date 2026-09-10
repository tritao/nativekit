import NativeKit;

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

		var event = new nk_event();
		event.set_struct_size(64);
		var polled = NativeKit.nk_poll_event(event);
		var eventOk = polled.status == 0 && polled.event.get_kind() == 0;
		NativeKit.nk_event_release(polled.event);
		var payloadOk = true;
		if (NativeKit.nk_clipboard_set_text("nativekit ffi") == 0) {
			var request = NativeKit.nk_clipboard_read_text();
			payloadOk = request.status == 0;
			var completed = false;
			for (_ in 0...1000) {
				var next = new nk_event();
				next.set_struct_size(64);
				var result = NativeKit.nk_poll_event(next);
				if (result.status != 0) {
					payloadOk = false;
					break;
				}
				if (result.event.get_kind() == 400) {
					var payload = result.event.get_data_bytes();
					payloadOk = payload.length == 13 && payload.get(0) == 110 && payload.get(12) == 105;
					NativeKit.nk_event_release(result.event);
					completed = true;
					break;
				}
				NativeKit.nk_event_release(result.event);
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
