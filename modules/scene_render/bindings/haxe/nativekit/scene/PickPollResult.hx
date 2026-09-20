package nativekit.scene;

import NativeKitGpu;

/** Result of polling an asynchronous GPU pick request. */
enum PickPollResult {
	Pending;
	Ready(result:PickResult);
	Stale;
	Failed(error:NativeKitGpu.GpuStatus);
}
