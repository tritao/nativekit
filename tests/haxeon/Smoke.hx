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
import NativeKit.MessageButtons;
import NativeKit.NotificationFlags;
import NativeKitRequestOutcome;

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
		if (NativeKit.nk_clipboard_set_text("nativekit ffi") == Result.Ok) {
			var completed = false, requests = new NativeKitRequests();
			var request = requests.readClipboardText(function(outcome) {
				payloadOk = switch outcome {
					case Success(text): text == "nativekit ffi";
					case _: false;
				};
				completed = true;
			});
			var duplicateRejected = false;
			try requests.track(request, function(_) {}) catch (_:Dynamic) duplicateRejected = true;
			payloadOk = payloadOk && duplicateRejected && requests.pending() == 1;
			for (_ in 0...1000) {
				requests.poll();
				if (completed)
					break;
			}
			payloadOk = payloadOk && completed && requests.pending() == 0
				&& !requests.cancelDialog(request) && !requests.forget(request);
			payloadOk = payloadOk && requests.pending() == 0;

			var forgottenCallbackCalled = false;
			var forgottenRequest = requests.readClipboardText(function(_) {
				forgottenCallbackCalled = true;
			});
			var forgottenRequestRemoved = requests.forget(forgottenRequest);
			var forgottenEventSeen = false;
			for (_ in 0...1000) {
				var value = requests.poll();
				forgottenEventSeen = switch value {
					case ClipboardText(completedRequest, _, _)
						if (Std.string(completedRequest) == Std.string(forgottenRequest)): true;
					case _: false;
				};
				if (forgottenEventSeen)
					break;
			}
			payloadOk = payloadOk && forgottenRequestRemoved && forgottenEventSeen
				&& !forgottenCallbackCalled && requests.pending() == 0;
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
		if (share.options.get_resource_count() != 1 || share.options.get_flags() != 0)
			return 13;
		var notification = NativeKitOptions.notification("NativeKit smoke", null, null, null,
			NotificationFlags.Silent);
		if (notification.get_flags() != NotificationFlags.Silent)
			return 18;
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
		var windowCreated = false;
		var dialogCancellationOk = true;
		try {
			var window = runtime.createWindow(windowOptions);
			windowCreated = true;
			windowOk = window.nativeHandle().isValid();
			var backendCapabilities:Capabilities = NativeKit.nk_get_capabilities();
			if (windowOk && backendCapabilities.contains(Capabilities.fileDialog())) {
				var requests = new NativeKitRequests();
				var fileCompleted = false, fileCancelled = false;
				var fileRequest = requests.openFile(window, NativeKitOptions.fileDialog("Cancellation smoke"),
					function(outcome) {
						fileCancelled = switch outcome {
							case Cancelled: true;
							case _: false;
						};
						fileCompleted = true;
					});
				var fileCancelStarted = requests.cancelDialog(fileRequest);
				var repeatedFileCancelRejected = !requests.cancelDialog(fileRequest);
				var polls = 0;
				while (!fileCompleted && polls < 1000) {
					requests.poll();
					polls++;
				}
				dialogCancellationOk = fileCancelStarted && repeatedFileCancelRejected
					&& fileCompleted && fileCancelled && requests.pending() == 0
					&& !requests.cancelDialog(fileRequest) && !requests.forget(fileRequest);

				if (dialogCancellationOk) {
					var messageCompleted = false, messageCancelled = false;
					var messageRequest = requests.messageDialog(window,
						NativeKitOptions.messageDialog("Cancellation smoke", null, null,
							MessageButtons.Yes | MessageButtons.No),
						function(outcome) {
							messageCancelled = switch outcome {
								case Cancelled: true;
								case _: false;
							};
							messageCompleted = true;
						});
					var messageCancelStarted = requests.cancelDialog(messageRequest);
					polls = 0;
					while (!messageCompleted && polls < 1000) {
						requests.poll();
						polls++;
					}
					dialogCancellationOk = messageCancelStarted && messageCompleted
						&& messageCancelled && requests.pending() == 0;
				}
			}
			window.dispose();
			windowOk = windowOk && window.isDisposed();
		} catch (error:Dynamic) {
			var unsupported = Std.string(error).indexOf("(-4)") >= 0;
			windowOk = !windowCreated && unsupported;
			if (windowCreated || !unsupported) dialogCancellationOk = false;
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
		if (!dialogCancellationOk)
			return 19;
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
