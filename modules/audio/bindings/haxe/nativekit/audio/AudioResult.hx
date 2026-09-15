package nativekit.audio;

import NativeKit;
import NativeKitError;

/** Provides consistent diagnostics for raw audio result calls. */
class AudioResult {
	public static function check(status:NativeKit.Result, operation:String):Void {
		if (status != NativeKit.Result.Ok)
			throw new NativeKitError(status, operation, NativeKit.nk_last_error());
	}
}
