import NativeKitEvent;
import NativeKit;

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
}
