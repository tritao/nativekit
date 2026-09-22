import nativekit.ffi.NativeKitConstants;
import nativekit.ffi.NativeKit;
import GraphicsImageRef;
import NativeKitEventValue;
import NativeKitEvents;
import NativeKitEventBytes;
import NativeKitEventDecoderTests;
import NativeKitTextInput;
import NativeKitRuntime;
import NativeKitWindow;
import NativeKitRequestOutcome;
import NativeFuture;
import NativePromise;

class Smoke {
	static function main():Int {
		var graphicsApi:GraphicsApi = GraphicsApi.Opengl;
		var unusedImageRef:Null<GraphicsImageRef> = null;
		if (unusedImageRef != null)
			return 19;
		if (graphicsApi != GraphicsApi.Opengl)
			return 17;
		var graphicsInfo = new GraphicsImageInfo();
		graphicsInfo.set_api(graphicsApi);
		if (NativeKit.nk_api_version() != NativeKitConstants.NK_API_VERSION)
			return 1;

		var capabilityMask = Capabilities.window().with(Capabilities.accessibility());
		if (!capabilityMask.contains(Capabilities.window()) || !capabilityMask.contains(Capabilities.accessibility())
			|| capabilityMask.without(Capabilities.window()).contains(Capabilities.window()))
			return 16;

		var initOptions = new InitOptions();
		initOptions.set_api_version(NativeKitConstants.NK_API_VERSION);
		initOptions.set_event_queue_capacity(32);
		var runtime = NativeKitRuntime.start(initOptions);
		if (runtime.isDisposed())
			return 2;
		var resultErrorOk = false;
		try
			NativeKit.nk_window_show_checked(WindowHandle.invalid(), true)
		catch (error:NativeKitError)
			resultErrorOk = error.result == Result.ErrorInvalidHandle && error.operation == "nk_window_show" && error.diagnostic != null;

		var events = runtime.events;
		var promise = new NativePromise<Int>();
		var futureValue = 0;
		promise.future.onComplete(function(outcome) switch outcome {
			case Success(value): futureValue = value;
			case _: futureValue = -1;
		});
		promise.complete(42);
		var cancelPromise = new NativePromise<Int>();
		cancelPromise.future.cancel();
		var futureOk = futureValue == 42 && promise.future.isComplete() && cancelPromise.future.isComplete();
		var lifetimeSubscription = events.listen(function(_) {});
		var eventOk = !events.poll();
		try {
			NativeKitEventBytes.decodeClipboardFiles(haxe.io.Bytes.alloc(4), 0);
			eventOk = false;
		} catch (_:Dynamic) {}
		var payloadOk = true;
		if (NativeKit.nk_clipboard_set_text("nativekit ffi") == Result.Ok) {
			var completed = false, requestSeenByListener = false;
			var requestSubscription:Null<NativeKitEventSubscription> = null;
			var requests = events.requests;
			var request = requests.readClipboardText(function(outcome) {
				payloadOk = switch outcome {
					case Success(text): text == "nativekit ffi";
					case _: false;
				};
				completed = true;
			});
			requestSubscription = events.listen(function(value) switch value {
				case ClipboardText(id, _, _) if (Std.string(id) == Std.string(request)):
					requestSeenByListener = completed;
				case _:
			});
			var duplicateRejected = false;
			try requests.track(request, function(_) {}) catch (_:Dynamic) duplicateRejected = true;
			payloadOk = payloadOk && duplicateRejected && requests.pending() == 1;
			for (_ in 0...1000) {
				events.poll();
				if (completed)
					break;
			}
			payloadOk = payloadOk && completed && requestSeenByListener && requests.pending() == 0
				&& !requests.cancel(request);
			payloadOk = payloadOk && requests.pending() == 0;
			if (requestSubscription != null)
				requestSubscription.dispose();
		}
		var fileArrayResult = NativeKit.nk_clipboard_set_files(["/tmp/nativekit-a", "/tmp/nativekit-b"]);
		if (fileArrayResult != 0 && fileArrayResult != Result.ErrorUnsupported)
			return 14;
		var resourceArrayResult = NativeKit.nk_clipboard_set_resources([
			resource("file:///tmp/nativekit-a", "text/plain", "nativekit-a")
		]);
		if (resourceArrayResult != 0 && resourceArrayResult != Result.ErrorUnsupported)
			return 15;

		var windowOptions = new WindowOptions();
		windowOptions.set_width(320);
		windowOptions.set_height(200);
		windowOptions.set_title("NativeKit smoke");
		windowOptions.set_flags(WindowFlags.Hidden);
		windowOptions.set_owner(WindowHandle.invalid());
		windowOptions.set_kind(WindowKind.Normal);
		var textFilter = new DialogFilter();
		textFilter.set_patterns("*.txt;*.md");
		textFilter.set_name("Têxt files");
		var imageFilter = new DialogFilter();
		imageFilter.set_patterns("*.png;*.jpg");
		imageFilter.set_name("Imágenes");
		var filters = new FileDialogOptions();
		filters.set_filters([textFilter, imageFilter]);
		if (filters.get_filter_count() != 2)
			return 12;
		var share = new ShareOptions();
		share.set_text("hello");
		share.set_title(null);
		share.set_flags(0);
		share.set_resources([resource("file:///tmp/nativekit.txt", "text/plain", "nativekit.txt")]);
		if (share.get_resource_count() != 1 || share.get_flags() != 0)
			return 13;
		var notification = new NotificationOptions();
		notification.set_title("NativeKit smoke");
		notification.set_body(null);
		notification.set_icon(null);
		notification.set_timeout_ms(0);
		notification.set_flags(NotificationFlags.Silent);
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
		try {
			var window = runtime.createWindow(windowOptions);
			windowOk = window.nativeHandle().isValid();
			window.setDecorationRegions([]);
			window.dispose();
			windowOk = windowOk && window.isDisposed();
		} catch (error:Dynamic) {
			windowOk = Std.string(error).indexOf("(-4)") >= 0;
		}
		var primary = NativeKit.nk_monitor_get_primary();
		var monitorOk = primary.status == -4;
		if (primary.status == 0) {
			var name = NativeKit.nk_monitor_get_name(primary.out_monitor);
			monitorOk = primary.out_monitor.isValid() && name.status == 0 && name.buffer != null;
		}

		var diagnosticOk = NativeKit.nk_window_destroy(WindowHandle.invalid()) == -3 && NativeKit.nk_last_error() != null;
		runtime.dispose();
		if (!events.isDisposed() || !lifetimeSubscription.isDisposed())
			return 21;
		if (!eventOk)
			return 3;
		if (!windowOk)
			return 4;
		if (!diagnosticOk)
			return 5;
		if (!resultErrorOk)
			return 19;
		if (!monitorOk)
			return 6;
		if (!futureOk)
			return 22;
		if (!payloadOk)
			return 7;
		if (!NativeKitEventDecoderTests.run())
			return 8;
		return 42;
	}

	static function resource(uri:String, mimeType:String, displayName:String):Resource {
		var value = new Resource();
		value.set_uri(uri);
		value.set_mime_type(mimeType);
		value.set_display_name(displayName);
		value.set_flags(0);
		return value;
	}
}
