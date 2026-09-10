import NativeKitEvent;

/** Maps asynchronous NativeKit request IDs to one-shot typed completions. */
class NativeKitRequests {
	final handlers:Map<String, NativeKitEventValue->Void> = [];

	public function new() {}

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
