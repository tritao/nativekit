import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
import NativeKitRequestOutcome;

/** A one-shot result completed by a NativeKit event. */
class NativeFuture<T> {
	var outcome:Null<NativeKitRequestOutcome<T>>;
	var cancellation:Null<Void->Void>;
	final callbacks:Array<NativeKitRequestOutcome<T>->Void> = [];

	public function new() {}

	public function onComplete(callback:NativeKitRequestOutcome<T>->Void):NativeFuture<T> {
		if (callback == null) throw "NativeFuture callback cannot be null";
		if (outcome != null) callback(outcome);
		else callbacks.push(callback);
		return this;
	}

	public function isComplete():Bool return outcome != null;

	/** Cancels the native operation when this future owns one. */
	public function cancel():Void {
		if (outcome != null) return;
		if (cancellation != null) cancellation();
		finish(Cancelled);
	}

	@:allow(NativeKitRequests)
	function setCancellation(action:Void->Void):Void cancellation = action;

	@:allow(NativePromise)
	function finish(value:NativeKitRequestOutcome<T>):Void {
		if (outcome != null) return;
		outcome = value;
		var pending = callbacks.copy();
		callbacks.resize(0);
		for (callback in pending) callback(value);
	}
}
