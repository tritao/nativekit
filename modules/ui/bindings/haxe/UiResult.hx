import NativeKitUI.NkuiResult;

/** Exception raised when a NativeKit UI operation returns an error. */
class UiError extends haxe.Exception {
	public final status:NkuiResult;
	public final operation:String;

	public function new(status:NkuiResult, operation:String) {
		this.status = status;
		this.operation = operation;
		super('$operation failed with NativeKit UI status $status');
	}
}

/** Centralizes raw UI result handling for the high-level graphics API. */
class UiResult {
	public static function check(status:NkuiResult, operation:String):Void {
		if (status != NkuiResult.Ok)
			throw new UiError(status, operation);
	}
}
