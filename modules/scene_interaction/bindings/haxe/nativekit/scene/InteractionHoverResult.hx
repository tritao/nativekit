package nativekit.scene;

import NativeKitGpu;

/** Result of polling the NativeKit interaction hover request. */
enum InteractionHoverResult {
	Idle;
	Pending;
	Ready(result:PickResult);
	Stale;
	Failed(error:NativeKitGpu.GpuStatus);
}
