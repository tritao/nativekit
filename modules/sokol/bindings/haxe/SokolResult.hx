import NativeKitSokol;

/** Centralizes raw Sokol-adapter result handling for the typed Haxe facade. */
class SokolResult {
	public static function check(status:Int, operation:String):Void {
		if (status != 0)
			throw '$operation failed with NativeKit-Sokol status $status: ${NativeKitSokol.nks_last_error()}';
	}
}
