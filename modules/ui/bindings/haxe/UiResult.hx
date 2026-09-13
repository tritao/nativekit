import NativeKitUI.UiStatus;

/** Exception raised when a NativeKit UI operation returns an error. */
class UiError extends haxe.Exception {
	public final status:UiStatus;
	public final operation:String;

	public function new(status:UiStatus, operation:String) {
		this.status = status;
		this.operation = operation;
		super('$operation failed with NativeKit UI status $status');
	}
}

/** Centralizes raw UI result handling for the high-level graphics API. */
class UiResult {
	public static function check(status:UiStatus, operation:String):Void {
		if (status != UiStatus.Ok)
			throw new UiError(status, operation);
	}
}
