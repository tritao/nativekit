/** Result of one asynchronous NativeKit request. */
enum NativeKitRequestOutcome<T> {
	/** The asynchronous operation completed successfully. */
	Success(value:T);
	/** A dialog was cancelled or dismissed without an accepted result. */
	Cancelled;
	/** The operation failed; `message` is optional backend-specific detail. */
	Failure(result:NativeKit.Result, message:Null<String>);
}
