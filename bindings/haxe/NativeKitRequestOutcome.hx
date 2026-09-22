import nativekit.ffi.NativeKit;
import nativekit.ffi.NativeKitTypes;
/** Result of one asynchronous NativeKit request. */
enum NativeKitRequestOutcome<T> {
	/** The asynchronous operation completed successfully. */
	Success(value:T);
	/** A user dismissed a dialog without selecting a result. */
	Cancelled;
	/** The operation failed; `message` is optional backend-specific detail. */
	Failure(result:Result, message:Null<String>);
}
