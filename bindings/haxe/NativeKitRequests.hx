import NativeKitEvent;
import NativeKitEventValue;
import NativeKit;
import NativeKit.MessageDialogOptions;
import NativeKit.MessageResult;
import NativeKit.Result;
import NativeKitEventValue.NativeKitResource;
import NativeKitOptions.NativeKitFileDialogOptions;
import NativeKitRequestOutcome;
import NativeKitWebView;
import NativeKitWindow;

/** Maps asynchronous NativeKit request IDs to one-shot typed completions. */
class NativeKitRequests {
	final handlers:Map<String, NativeKitEventValue->Bool->Void> = [];
	final dialogCancellationRequested:Map<String, Bool> = [];

	public function new() {}

	/** Starts a clipboard-text read and tracks its typed completion. */
	public function readClipboardText(handler:NativeKitRequestOutcome<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_text();
		checkStarted("clipboard text read", started.status);
		track(started.out_request, function(value) switch value {
			case ClipboardText(_, result, text): handler(resultOutcome(result, text));
			case _: throw "NativeKit clipboard request completed with the wrong event";
		});
		return started.out_request;
	}

	/** Starts a clipboard-file read and tracks its typed completion. */
	public function readClipboardFiles(handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_files();
		checkStarted("clipboard file read", started.status);
		track(started.out_request, function(value) switch value {
			case ClipboardFiles(_, result, paths): handler(resultOutcome(result, paths));
			case _: wrongEvent("clipboard file read");
		});
		return started.out_request;
	}

	/** Starts a structured-resource clipboard read and tracks its typed completion. */
	public function readClipboardResources(
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_resources();
		checkStarted("clipboard resource read", started.status);
		track(started.out_request, function(value) switch value {
			case Resources(_, _, result, _, items): handler(resultOutcome(result, items));
			case _: wrongEvent("clipboard resource read");
		});
		return started.out_request;
	}

	public function openFile(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_file(parent.nativeHandle(), configured.options);
		return trackDialog("open-file dialog", started.status, started.out_request, handler);
	}

	public function saveFile(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_file(parent.nativeHandle(), configured.options);
		return trackDialog("save-file dialog", started.status, started.out_request, handler);
	}

	public function selectDirectory(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_directory(parent.nativeHandle(), configured.options);
		return trackDialog("directory dialog", started.status, started.out_request, handler);
	}

	public function openResource(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_resource(parent.nativeHandle(), configured.options);
		return trackResourceDialog("open-resource dialog", started.status, started.out_request, handler);
	}

	public function saveResource(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_resource(parent.nativeHandle(), configured.options);
		return trackResourceDialog("save-resource dialog", started.status, started.out_request, handler);
	}

	public function selectResourceDirectory(parent:NativeKitWindow, configured:NativeKitFileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_resource_directory(parent.nativeHandle(), configured.options);
		return trackResourceDialog("resource-directory dialog", started.status, started.out_request, handler);
	}

	public function messageDialog(parent:NativeKitWindow, options:MessageDialogOptions,
		handler:NativeKitRequestOutcome<MessageResult>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_message(parent.nativeHandle(), options);
		checkStarted("message dialog", started.status);
		trackDialogRequest(started.out_request, function(value, cancellationRequested) switch value {
			case DialogMessage(_, result, button):
				handler(messageOutcome(result, button, cancellationRequested));
			case _: wrongEvent("message dialog");
		});
		return started.out_request;
	}

	public function evaluateWebView(webview:NativeKitWebView, script:String,
		handler:NativeKitRequestOutcome<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_webview_eval(webview.nativeHandle(), script);
		checkStarted("WebView evaluation", started.status);
		track(started.out_request, function(value) switch value {
			case WebViewEvaluation(_, _, result, json):
				handler(resultOutcome(result, json, result == Result.Ok ? null : json));
			case _: wrongEvent("WebView evaluation");
		});
		return started.out_request;
	}

	public function track(request:haxe.Int64, handler:NativeKitEventValue->Void):Void {
		var key = Std.string(request);
		if (handlers.exists(key)) throw "NativeKit request is already tracked";
		handlers.set(key, function(value, _cancelRequested) {
			handler(value);
		});
	}

	/**
	 * Cancels a dialog started by this manager. Its completion remains tracked
	 * and is delivered as Cancelled. Returns false if it is not tracked here or
	 * cancellation has already been requested; native errors are thrown.
	 */
	public function cancelDialog(request:haxe.Int64):Bool {
		var key = Std.string(request);
		if (!handlers.exists(key) || !dialogCancellationRequested.exists(key)
			|| dialogCancellationRequested.get(key))
			return false;
		var result = NativeKit.nk_dialog_cancel(request);
		NativeKitResult.check(result, "dialog.cancel");
		dialogCancellationRequested.set(key, true);
		return true;
	}

	/** Stops dispatching a tracked completion without cancelling native work. */
	public function forget(request:haxe.Int64):Bool {
		var key = Std.string(request);
		dialogCancellationRequested.remove(key);
		return handlers.remove(key);
	}

	/** Polls, decodes, releases, and dispatches one terminal request event. */
	public function poll():NativeKitEventValue {
		var event = NativeKitEvent.poll(), value = event.take(), key = Std.string(event.request);
		var handler = handlers.get(key);
		if (handler != null) {
			var cancellationRequested = dialogCancellationRequested.get(key) == true;
			handlers.remove(key);
			dialogCancellationRequested.remove(key);
			handler(value, cancellationRequested);
		}
		return value;
	}

	public function pending():Int {
		var count = 0;
		for (_ in handlers) count++;
		return count;
	}

	function trackDialog(name:String, status:Result, request:haxe.Int64,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		checkStarted(name, status);
		trackDialogRequest(request, function(value, _cancelRequested) switch value {
			case DialogPaths(_, result, accepted, paths):
				handler(acceptedOutcome(result, accepted, paths));
			case _: wrongEvent(name);
		});
		return request;
	}

	function trackResourceDialog(name:String, status:Result, request:haxe.Int64,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		checkStarted(name, status);
		trackDialogRequest(request, function(value, _cancelRequested) switch value {
			case Resources(kind, _, result, accepted, items):
				if (kind != NativeKit.EventKind.DialogResourcesComplete) wrongEvent(name);
				handler(acceptedOutcome(result, accepted, items));
			case _: wrongEvent(name);
		});
		return request;
	}

	function trackDialogRequest(request:haxe.Int64,
		handler:NativeKitEventValue->Bool->Void):Void {
		var key = Std.string(request);
		if (handlers.exists(key)) throw "NativeKit request is already tracked";
		handlers.set(key, handler);
		dialogCancellationRequested.set(key, false);
	}

	static function checkStarted(name:String, result:Result):Void
		NativeKitResult.check(result, 'NativeKit $name');

	static function resultOutcome<T>(result:Result, value:T,
		?message:Null<String>):NativeKitRequestOutcome<T> {
		return result == Result.Ok ? Success(value) : Failure(result, message);
	}

	static function acceptedOutcome<T>(result:Result, accepted:Bool,
		value:T):NativeKitRequestOutcome<T> {
		if (result != Result.Ok)
			return Failure(result, null);
		return accepted ? Success(value) : Cancelled;
	}

	static function messageOutcome(result:Result, button:MessageResult,
		cancellationRequested:Bool):NativeKitRequestOutcome<MessageResult> {
		if (result != Result.Ok)
			return Failure(result, null);
		return cancellationRequested || button == MessageResult.None ? Cancelled : Success(button);
	}

	static function wrongEvent(name:String):Void
		throw 'NativeKit $name completed with the wrong event';
}
