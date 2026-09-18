import NativeKit;
import NativeKitError;

/** Managed owner for a native task handle; it never supplies a worker callback. */
class NativeTask {
	final value:NativeKit.TaskHandle;
	var disposed:Bool = false;

	private function new(value:NativeKit.TaskHandle) this.value = value;

	/** Adopts a handle returned by native code or a Haxeon native trampoline. */
	public static function adopt(value:NativeKit.TaskHandle):NativeTask {
		if (value == null || value.rawValue() == 0) throw "NativeTask cannot adopt an invalid handle";
		return new NativeTask(value);
	}

	public function nativeHandle():NativeKit.TaskHandle {
		ensureLive();
		return value;
	}

	public function cancel():Void {
		ensureLive();
		check(NativeKit.nk_task_cancel(value), "task.cancel");
	}

	public function state():NativeKit.TaskState {
		ensureLive();
		return NativeKit.nk_task_get_state_checked(value);
	}

	public function dispose():Void {
		if (disposed) return;
		disposed = true;
		check(NativeKit.nk_task_destroy(value), "task.dispose");
	}

	public function isDisposed():Bool return disposed;

	function ensureLive():Void if (disposed) throw "NativeTask has been disposed";

	static function check(status:NativeKit.Result, operation:String):Void
		if (status != NativeKit.Result.Ok)
			throw new NativeKitError(status, operation, NativeKit.nk_last_error());
}
