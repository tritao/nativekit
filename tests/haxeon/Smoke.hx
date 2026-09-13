import NativeKit;
import NativeKit.Result;
import NativeKitEvent;
import NativeKitEventValue;
import NativeKitRequests;
import NativeKitEventBytes;
import NativeKitEventDecoderTests;
import NativeKitTextInput;
import NativeKitOptions;
import NativeKitRuntime;
import NativeKitWindow;
import NativeKit.NativeKitConstants;
import NativeKit.InitOptions;
import NativeKit.TextInputState;
import NativeKit.Capabilities;

class Smoke {
	static function main():Int {
		if (NativeKit.nk_api_version() != NativeKitConstants.NK_API_VERSION)
			return 1;

		var capabilityMask = Capabilities.window().with(Capabilities.accessibility());
		if (!capabilityMask.contains(Capabilities.window()) || !capabilityMask.contains(Capabilities.accessibility())
			|| capabilityMask.without(Capabilities.window()).contains(Capabilities.window()))
			return 16;

		var runtime = NativeKitRuntime.start(NativeKitOptions.init(32));
		if (runtime.isDisposed())
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
			NativeKitEventBytes.decodeClipboardFiles(haxe.io.Bytes.alloc(4), 0);
			eventOk = false;
		} catch (_:Dynamic) {}
		var payloadOk = true;
		if (NativeKit.nk_clipboard_set_text("nativekit ffi") == 0) {
			var request = NativeKit.nk_clipboard_read_text();
			payloadOk = request.status == 0;
			var completed = false, requests = new NativeKitRequests();
			requests.track(request.out_request, function(value) {
				payloadOk = switch value {
					case ClipboardText(completedRequest, completedResult, text): completedResult == 0 && text == "nativekit ffi";
					case _: false;
				};
				completed = true;
			});
			var duplicateRejected = false;
			try requests.track(request.out_request, function(_) {}) catch (_:Dynamic) duplicateRejected = true;
			payloadOk = payloadOk && duplicateRejected && requests.pending() == 1;
			for (_ in 0...1000) {
				requests.poll();
				if (completed)
					break;
			}
			payloadOk = payloadOk && completed && requests.pending() == 0 && !requests.cancel(request.out_request);
			payloadOk = payloadOk && requests.pending() == 0;
		}
		var fileArrayResult = NativeKit.nk_clipboard_set_files(["/tmp/nativekit-a", "/tmp/nativekit-b"]);
		if (fileArrayResult != 0 && fileArrayResult != Result.ErrorUnsupported)
			return 14;
		var resourceArrayResult = NativeKit.nk_clipboard_set_resources([
			NativeKitOptions.resource("file:///tmp/nativekit-a", "text/plain", "nativekit-a")
		]);
		if (resourceArrayResult != 0 && resourceArrayResult != Result.ErrorUnsupported)
			return 15;

		var windowOptions = NativeKitOptions.window(320, 200, "NativeKit smoke", 2);
		var filters = NativeKitOptions.filteredFileDialog([
			NativeKitOptions.dialogFilter("*.txt;*.md", "Têxt files"),
			NativeKitOptions.dialogFilter("*.png;*.jpg", "Imágenes")
		]);
		if (filters.options.get_filter_count() != 2)
			return 12;
		var share = NativeKitOptions.resourceShare([
			NativeKitOptions.resource("file:///tmp/nativekit.txt", "text/plain", "nativekit.txt")
		], "hello");
		if (share.options.get_resource_count() != 1)
			return 13;
		if (windowOptions.get_title() != "NativeKit smoke")
			return 9;
		windowOptions.set_title(null);
		if (windowOptions.get_title() != null)
			return 10;
		var textState = NativeKitTextInput.state("first", 0, 5, 5, 5);
		textState.set_text("olá 👋");
		if (textState.get_text() != "olá 👋" || textState.get_struct_size() != TextInputState.size())
			return 11;
		var windowOk = false;
		try {
			var window = runtime.createWindow(windowOptions);
			windowOk = window.nativeHandle().isValid();
			window.dispose();
			windowOk = windowOk && window.isDisposed();
		} catch (error:Dynamic) {
			windowOk = Std.string(error).indexOf("(-4)") >= 0;
		}
		var primary = NativeKit.nk_monitor_get_primary();
		var monitorOk = primary.status == -4;
		if (primary.status == 0) {
			var name = NativeKit.nk_monitor_get_name(primary.out_monitor);
			monitorOk = primary.out_monitor != 0 && name.status == 0 && name.buffer != null;
		}

		var diagnosticOk = NativeKit.nk_window_destroy(0) == -3 && NativeKit.nk_last_error() != null;
		runtime.dispose();
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
		if (!NativeKitEventDecoderTests.run())
			return 8;
		return 42;
	}
}
