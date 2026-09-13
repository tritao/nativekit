import NativeKit;
import NativeKit.Result;

/** Adds the NativeKit thread-local diagnostic to a failed typed operation. */
class NativeKitResult {
	public static function check(status:Result, operation:String):Void {
		if (status != Result.Ok) {
			var detail = NativeKit.nk_last_error();
			throw '$operation failed ($status)${detail == null || detail.length == 0 ? "" : ": " + detail}';
		}
	}
}
