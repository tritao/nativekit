import NativeKitEvent;
import NativeKitEventValue;
import NativeKit;
import NativeKit.MessageDialogOptions;
import NativeKitEventValue.NativeKitResource;
import NativeKitOptions.NativeKitFileDialogOptions;

/** Stable typed result of a completed NativeKit message dialog. */
enum abstract NativeKitMessageResult(Int) from Int to Int {
	var None = 0;
	var Ok = 1;
	var Cancel = 2;
	var Yes = 3;
	var No = 4;
}

/** Maps asynchronous NativeKit request IDs to one-shot typed completions. */
class NativeKitRequests {
	final handlers:Map<String, NativeKitEventValue->Void> = [];

	public function new() {}

	/** Starts a clipboard-text read and tracks its typed completion. */
	public function readClipboardText(handler:String->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_text();
		if (started.status != 0) throw 'NativeKit clipboard read failed: ${started.status}';
		track(started.out_request, function(value) switch value {
			case ClipboardText(_, result, text): if (result == 0) handler(text); else throw 'NativeKit clipboard completion failed: $result';
			case _: throw "NativeKit clipboard request completed with the wrong event";
		});
		return started.out_request;
	}

	/** Starts a clipboard-file read and tracks its typed completion. */
	public function readClipboardFiles(handler:Array<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_files();
		checkStarted("clipboard file read", started.status);
		track(started.out_request, function(value) switch value {
			case ClipboardFiles(_, result, paths): checkCompleted("clipboard file read", result); handler(paths);
			case _: wrongEvent("clipboard file read");
		});
		return started.out_request;
	}

	/** Starts a structured-resource clipboard read and tracks its typed completion. */
	public function readClipboardResources(handler:Array<NativeKitResource>->Void):haxe.Int64 {
		var started = NativeKit.nk_clipboard_read_resources();
		checkStarted("clipboard resource read", started.status);
		track(started.out_request, function(value) switch value {
			case Resources(_, _, result, _, items): checkCompleted("clipboard resource read", result); handler(items);
			case _: wrongEvent("clipboard resource read");
		});
		return started.out_request;
	}

	public function openFile(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_file(parent, configured.options);
		return trackDialog("open-file dialog", started.status, started.out_request, handler);
	}

	public function saveFile(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_file(parent, configured.options);
		return trackDialog("save-file dialog", started.status, started.out_request, handler);
	}

	public function selectDirectory(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<String>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_directory(parent, configured.options);
		return trackDialog("directory dialog", started.status, started.out_request, handler);
	}

	public function openResource(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<NativeKitResource>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_open_resource(parent, configured.options);
		return trackResourceDialog("open-resource dialog", started.status, started.out_request, handler);
	}

	public function saveResource(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<NativeKitResource>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_save_resource(parent, configured.options);
		return trackResourceDialog("save-resource dialog", started.status, started.out_request, handler);
	}

	public function selectResourceDirectory(parent:Int, configured:NativeKitFileDialogOptions, handler:Bool->Array<NativeKitResource>->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_select_resource_directory(parent, configured.options);
		return trackResourceDialog("resource-directory dialog", started.status, started.out_request, handler);
	}

	public function messageDialog(parent:Int, options:MessageDialogOptions, handler:NativeKitMessageResult->Void):haxe.Int64 {
		var started = NativeKit.nk_dialog_message(parent, options);
		checkStarted("message dialog", started.status);
		track(started.out_request, function(value) switch value {
			case DialogMessage(_, result, button): checkCompleted("message dialog", result); handler(cast button);
			case _: wrongEvent("message dialog");
		});
		return started.out_request;
	}

	public function evaluateWebView(webview:Int, script:String, handler:String->Void):haxe.Int64 {
		var started = NativeKit.nk_webview_eval(webview, script);
		checkStarted("WebView evaluation", started.status);
		track(started.out_request, function(value) switch value {
			case WebViewEvaluation(_, _, result, json): checkCompleted("WebView evaluation", result); handler(json);
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

	/** Polls, decodes, releases, and dispatches one terminal request event. */
	public function poll():NativeKitEventValue {
		var event = NativeKitEvent.poll(), value = event.take(), key = Std.string(event.request);
		var handler = handlers.get(key);
		if (handler != null) {
			handlers.remove(key);
			handler(value);
		}
		return value;
	}

	public function pending():Int {
		var count = 0;
		for (_ in handlers) count++;
		return count;
	}

	function trackDialog(name:String, status:Int, request:haxe.Int64, handler:Bool->Array<String>->Void):haxe.Int64 {
		checkStarted(name, status);
		track(request, function(value) switch value {
			case DialogPaths(_, result, accepted, paths): checkCompleted(name, result); handler(accepted, paths);
			case _: wrongEvent(name);
		});
		return request;
	}

	function trackResourceDialog(name:String, status:Int, request:haxe.Int64, handler:Bool->Array<NativeKitResource>->Void):haxe.Int64 {
		checkStarted(name, status);
		track(request, function(value) switch value {
			case Resources(kind, _, result, accepted, items):
				if (kind != NativeKit.EventKind.DialogResourcesComplete) wrongEvent(name);
				checkCompleted(name, result); handler(accepted, items);
			case _: wrongEvent(name);
		});
		return request;
	}

	static function checkStarted(name:String, result:Int):Void
		if (result != 0) throw 'NativeKit $name failed to start: $result';

	static function checkCompleted(name:String, result:Int):Void
		if (result != 0) throw 'NativeKit $name completion failed: $result';

	static function wrongEvent(name:String):Void
		throw 'NativeKit $name completed with the wrong event';
}
