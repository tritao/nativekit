import nativekit.ffi.NativeKit;
import NativeKitRequestOutcome;

/** Internal completion owner used by NativeKit task/request adapters. */
class NativePromise<T> {
	public final future:NativeFuture<T>;

	public function new() future = new NativeFuture<T>();

	public function complete(value:T):Void future.finish(Success(value));
	public function fail(result:Result, message:Null<String>):Void
		future.finish(Failure(result, message));
	public function cancel():Void future.finish(Cancelled);
}
