package nativekit.ui.core;

import NativeKit;
import NativeKit.Result;
import NativeKitEventValue;

/** UI-thread clipboard bridge with asynchronous paste completion routing. */
class ClipboardService {
	final pending:Map<String, String->Void>;
	var disposed:Bool;

	public function new() {
		pending = new Map();
		disposed = false;
	}

	/** Copies UTF-8 text to the system clipboard. */
	public function writeText(text:String):Void {
		ensureLive();
		NativeKitResult.check(NativeKit.nk_clipboard_set_text(text == null ? "" : text),
			"ui.clipboard.writeText");
	}

	/** Starts an asynchronous clipboard read and dispatches its text to `handler`. */
	public function readText(handler:String->Void):haxe.Int64 {
		ensureLive();
		if (handler == null)
			throw "Clipboard reads require a completion handler";
		var started = NativeKit.nk_clipboard_read_text();
		NativeKitResult.check(started.status, "ui.clipboard.readText");
		trackRead(started.out_request, handler);
		return started.out_request;
	}

	/** Tracks a host-started clipboard request using the same completion path. */
	public function trackRead(request:haxe.Int64, handler:String->Void):Void {
		ensureLive();
		if (handler == null)
			throw "Clipboard reads require a completion handler";
		var key = Std.string(request);
		if (pending.exists(key))
			throw "Clipboard request is already tracked";
		pending.set(key, handler);
	}

	/** Consumes a decoded clipboard completion only when this service owns its request. */
	public function consume(event:NativeKitEventValue):Bool {
		ensureLive();
		return switch (event) {
			case ClipboardText(request, result, text):
				var key = Std.string(request);
				var handler = pending.get(key);
				if (handler == null)
					false;
				else {
					pending.remove(key);
					if (result == Result.Ok)
						handler(text == null ? "" : text);
					true;
				}
			case _:
				false;
		};
	}

	public function dispose():Void {
		if (disposed)
			return;
		pending.clear();
		disposed = true;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Clipboard service has been disposed";
	}
}
