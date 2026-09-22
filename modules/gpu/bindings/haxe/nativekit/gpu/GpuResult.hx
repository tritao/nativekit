package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;

/** Centralizes raw GPU-adapter result handling for the typed Haxe facade. */
class GpuResult {
	public static function check(status:Int, operation:String):Void {
		if (status != 0)
			throw '$operation failed with NativeKit-GPU status $status: ${NativeKitGpu.nkgpu_last_error()}';
	}
}
