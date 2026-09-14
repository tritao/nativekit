import NativeKitEventValue;
import NativeKit;
import NativeKit.MessageDialogOptions;
import NativeKit.MessageResult;
import NativeKit.Result;
import NativeKit.FileDialogOptions;
import NativeKitEventValue.NativeKitResource;
import NativeKitRequestOutcome;
import NativeKitWebView;
import NativeKitWindow;

/** Maps asynchronous NativeKit request IDs to one-shot typed completions. */
class NativeKitRequests {
	final handlers:Map<String, NativeKitEventValue->Void> = [];

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

	public function openFile(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_file(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackDialog("open-file dialog", started.status, started.out_request, handler);
	}

	public function saveFile(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_file(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackDialog("save-file dialog", started.status, started.out_request, handler);
	}

	public function selectDirectory(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_directory(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackDialog("directory dialog", started.status, started.out_request, handler);
	}

	public function openResource(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_resource(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("open-resource dialog", started.status, started.out_request, handler);
	}

	public function saveResource(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_resource(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("save-resource dialog", started.status, started.out_request, handler);
	}

	public function selectResourceDirectory(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_resource_directory(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("resource-directory dialog", started.status, started.out_request, handler);
	}

	public function messageDialog(parent:NativeKitWindow, options:MessageDialogOptions,
		handler:NativeKitRequestOutcome<MessageResult>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_message(new NativeKit.Handle(parent.nativeHandle().rawValue()), options);
		checkStarted("message dialog", started.status);
		track(started.out_request, function(value) switch value {
			case DialogMessage(_, result, button):
				handler(acceptedOutcome(result, button != MessageResult.None, button));
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
		handlers.set(key, handler);
	}

	public function cancel(request:haxe.Int64):Bool
		return handlers.remove(Std.string(request));

	/** Routes a decoded event to its matching one-shot request handler. */
	public function handle(value:NativeKitEventValue):Bool {
		var key = requestKey(value);
		if (key == null)
			return false;
		var handler = handlers.get(key);
		if (handler == null)
			return false;
		handlers.remove(key);
		handler(value);
		return true;
	}

	public function pending():Int {
		var count = 0;
		for (_ in handlers) count++;
		return count;
	}

	function trackDialog(name:String, status:Result, request:haxe.Int64,
		handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		checkStarted(name, status);
		track(request, function(value) switch value {
			case DialogPaths(_, result, accepted, paths):
				handler(acceptedOutcome(result, accepted, paths));
			case _: wrongEvent(name);
		});
		return request;
	}

	function trackResourceDialog(name:String, status:Result, request:haxe.Int64,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		checkStarted(name, status);
		track(request, function(value) switch value {
			case Resources(kind, _, result, accepted, items):
				if (kind != NativeKit.EventKind.DialogResourcesComplete) wrongEvent(name);
				handler(acceptedOutcome(result, accepted, items));
			case _: wrongEvent(name);
		});
		return request;
	}

	static function requestKey(value:NativeKitEventValue):Null<String> {
		var key:Null<String> = switch value {
			case ClipboardText(id, _, _): Std.string(id);
			case ClipboardFiles(id, _, _): Std.string(id);
			case DialogPaths(id, _, _, _): Std.string(id);
			case DialogMessage(id, _, _): Std.string(id);
			case WebViewEvaluation(_, id, _, _): Std.string(id);
			case WebViewNavigationRequest(_, id, _): Std.string(id);
			case NotificationActivated(id, _): Std.string(id);
			case NotificationFailed(id, _): Std.string(id);
			case Resources(_, id, _, _, _): Std.string(id);
			case _: null;
		};
		return key == "0" ? null : key;
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

	static function wrongEvent(name:String):Void
		throw 'NativeKit $name completed with the wrong event';
}
