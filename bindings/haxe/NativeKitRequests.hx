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
	final taskHandlers:Map<String, NativeKitEventValue->Void> = [];

	public function new() {}

	/** Starts a clipboard-text read and tracks its typed completion. */
	public function readClipboardText(handler:NativeKitRequestOutcome<String>->Void):haxe.Int64 {
		var request = NativeKit.nk_clipboard_read_text_checked();
		track(request, function(value) switch value {
			case ClipboardText(_, result, text): handler(resultOutcome(result, text));
			case _: throw "NativeKit clipboard request completed with the wrong event";
		});
		return request;
	}

	/** Starts a clipboard-file read and tracks its typed completion. */
	public function readClipboardFiles(handler:NativeKitRequestOutcome<Array<String>>->Void):haxe.Int64 {
		var request = NativeKit.nk_clipboard_read_files_checked();
		track(request, function(value) switch value {
			case ClipboardFiles(_, result, paths): handler(resultOutcome(result, paths));
			case _: wrongEvent("clipboard file read");
		});
		return request;
	}

	/** Starts a structured-resource clipboard read and tracks its typed completion. */
	public function readClipboardResources(
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var request = NativeKit.nk_clipboard_read_resources_checked();
		track(request, function(value) switch value {
			case Resources(_, _, result, _, items): handler(resultOutcome(result, items));
			case _: wrongEvent("clipboard resource read");
		});
		return request;
	}

	public function openResource(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var request = NativeKit.nk_dialog_open_resource_checked(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("open-resource dialog", request, handler);
	}

	public function saveResource(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var request = NativeKit.nk_dialog_save_resource_checked(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("save-resource dialog", request, handler);
	}

	public function selectResourceDirectory(parent:NativeKitWindow, configured:FileDialogOptions,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
		var request = NativeKit.nk_dialog_select_resource_directory_checked(new NativeKit.Handle(parent.nativeHandle().rawValue()), configured);
		return trackResourceDialog("resource-directory dialog", request, handler);
	}

	public function messageDialog(parent:NativeKitWindow, options:MessageDialogOptions,
		handler:NativeKitRequestOutcome<MessageResult>->Void):haxe.Int64 {
		var request = NativeKit.nk_dialog_message_checked(new NativeKit.Handle(parent.nativeHandle().rawValue()), options);
		track(request, function(value) switch value {
			case DialogMessage(_, result, button):
				handler(acceptedOutcome(result, button != MessageResult.None, button));
			case _: wrongEvent("message dialog");
		});
		return request;
	}

	public function evaluateWebView(webview:NativeKitWebView, script:String,
		handler:NativeKitRequestOutcome<String>->Void):haxe.Int64 {
		var request = NativeKit.nk_webview_eval_checked(webview.nativeHandle(), script);
		track(request, function(value) switch value {
			case WebViewEvaluation(_, _, result, json):
				handler(resultOutcome(result, json, result == Result.Ok ? null : json));
			case _: wrongEvent("WebView evaluation");
		});
		return request;
	}

	public function track(request:haxe.Int64, handler:NativeKitEventValue->Void):Void {
		var key = Std.string(request);
		if (handlers.exists(key)) throw "NativeKit request is already tracked";
		handlers.set(key, handler);
	}

	public function cancel(request:haxe.Int64):Bool
		return handlers.remove(Std.string(request));

	/** Tracks a native task without installing a managed worker callback. */
	public function trackTask<T>(task:NativeTask, decode:haxe.io.Bytes->T):NativeFuture<T> {
		var promise = new NativePromise<T>();
		var key = Std.string(task.nativeHandle().rawValue());
		if (taskHandlers.exists(key)) throw "NativeKit task is already tracked";
		promise.future.setCancellation(function() {
			try task.cancel() catch (_:Dynamic) {}
		});
		taskHandlers.set(key, function(value) switch value {
			case TaskProgress(_, _): null;
			case TaskComplete(_, result, data):
				if (result == Result.Ok) promise.complete(decode(data));
				else promise.fail(result, null);
			case TaskFailed(_, result, _): promise.fail(result, null);
			case TaskCancelled(_, _, _): promise.cancel();
			case _: throw "NativeKit task completed with the wrong event";
		});
		return promise.future;
	}

	/** Routes a decoded event to its matching one-shot request handler. */
	public function handle(value:NativeKitEventValue):Bool {
		var key = requestKey(value);
		if (key == null)
			return false;
		var handler = handlers.get(key);
		if (handler != null) {
			handlers.remove(key);
			handler(value);
			return true;
		}
		var taskKey:Null<String> = switch value {
			case TaskProgress(source, _): Std.string(source.rawValue());
			case TaskComplete(source, _, _): Std.string(source.rawValue());
			case TaskFailed(source, _, _): Std.string(source.rawValue());
			case TaskCancelled(source, _, _): Std.string(source.rawValue());
			case _: null;
		};
		if (taskKey == null)
			return false;
		handler = taskHandlers.get(taskKey);
		if (handler == null)
			return false;
		var terminal = switch value {
			case TaskComplete(_, _, _) | TaskFailed(_, _, _) | TaskCancelled(_, _, _): true;
			case _: false;
		};
		if (terminal)
			taskHandlers.remove(taskKey);
		handler(value);
		return true;
	}

	public function pending():Int {
		var count = 0;
		for (_ in handlers) count++;
		for (_ in taskHandlers) count++;
		return count;
	}

	function trackResourceDialog(name:String, request:haxe.Int64,
		handler:NativeKitRequestOutcome<Array<NativeKitResource>>->Void):haxe.Int64 {
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
			case DialogMessage(id, _, _): Std.string(id);
			case WebViewEvaluation(_, id, _, _): Std.string(id);
			case WebViewNavigationRequest(_, id, _): Std.string(id);
			case NotificationActivated(id, _): Std.string(id);
			case NotificationFailed(id, _): Std.string(id);
			case Resources(_, id, _, _, _): Std.string(id);
			case ResourceAssetReady(_, id): Std.string(id);
			case ResourceAssetLoadFailed(_, id, _): Std.string(id);
			case AudioClipReady(_, id): Std.string(id);
			case AudioClipLoadFailed(_, id, _): Std.string(id);
			case _: null;
		};
		return key == "0" ? null : key;
	}

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
