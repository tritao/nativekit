import NativeKitUI.Nkui_result;

/** Exception raised when a NativeKit UI operation returns an error. */
class UiError extends haxe.Exception {
	public final status:Nkui_result;
	public final operation:String;

	public function new(status:Nkui_result, operation:String) {
		this.status = status;
		this.operation = operation;
		super('$operation failed with NativeKit UI status $status');
	}
}

/** Centralizes raw UI result handling for the high-level graphics API. */
class UiResult {
	public static function check(status:Nkui_result, operation:String):Void {
		if (status != Nkui_result.NKUI_OK)
			throw new UiError(status, operation);
	}
}
