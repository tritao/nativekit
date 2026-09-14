package nativekit.ui.semantics;

import NativeKit;

/** Complete Haxe-owned input for one native accessibility update call. */
class AccessibilityUpdateBatch {
	public final nativeUpdate:NativeKit.AccessibilityUpdate;
	/** Packed little-endian 32-bit node IDs consumed by the core binding helper. */
	public final removedNodeIds:haxe.io.Bytes;
	public final removedNodeCount:Int;

	public function new(nativeUpdate:NativeKit.AccessibilityUpdate,
			removedNodeIds:haxe.io.Bytes, removedNodeCount:Int) {
		this.nativeUpdate = nativeUpdate;
		this.removedNodeIds = removedNodeIds;
		this.removedNodeCount = removedNodeCount;
	}
}
