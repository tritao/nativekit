import NativeKit;
import NativeKit.Result;

/** Adds the NativeKit thread-local diagnostic to a failed typed operation. */
class NativeKitResult {
	public static function check(status:Result, operation:String):Void {
		if (status != Result.Ok)
			throw new NativeKitError(status, operation, NativeKit.nk_last_error());
	}
}
