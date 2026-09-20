package nativekit.gpu;

import NativeKitGpu;
import nativekit.gpu.Enums.TimestampState;

/** Owns one asynchronous GPU timestamp interval. */
class Timestamp {
	final renderer:Renderer;
	final value:nkgpu_timestamp;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_timestamp) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	/** Begins timing work in the active render or compute pass. */
	public static function begin(renderer:Renderer):Timestamp {
		renderer.ensureFrame();
		var made = NativeKitGpu.nkgpu_timestamp_begin(renderer.nativeHandle());
		GpuResult.check(made.status, "timestamp.begin");
		return new Timestamp(renderer, made.out_timestamp);
	}

	/** Begins a named timing scope in the active render or compute pass. */
	public static function beginNamed(renderer:Renderer, label:String):Timestamp {
		if (label == null)
			throw "GPU timestamp label must not be null";
		renderer.ensureFrame();
		var desc = new nkgpu_timestamp_desc();
		desc.set_struct_size(16);
		desc.set_label(label);
		var made = NativeKitGpu.nkgpu_timestamp_begin_desc(renderer.nativeHandle(), desc);
		GpuResult.check(made.status, "timestamp.beginNamed");
		return new Timestamp(renderer, made.out_timestamp);
	}

	public function end():Void {
		ensureLive();
		renderer.ensureFrame();
		GpuResult.check(NativeKitGpu.nkgpu_timestamp_end(renderer.nativeHandle(), value),
			"timestamp.end");
	}

	public function query():TimestampInfo {
		ensureLive();
		renderer.ensureResourceOperation();
		var result = NativeKitGpu.nkgpu_timestamp_query(renderer.nativeHandle(), value);
		GpuResult.check(result.status, "timestamp.query");
		return TimestampInfo.fromNative(result.out_info);
	}

	/** Returns the copied scope label, or an empty string for an unnamed scope. */
	public function label():String {
		ensureLive();
		renderer.ensureResourceOperation();
		return NativeKitGpu.nkgpu_timestamp_get_label(renderer.nativeHandle(), value);
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_timestamp_destroy(renderer.nativeHandle(), value),
			"timestamp.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "GPU timestamp has been disposed";
		renderer.ensureLive();
	}
}

/** Snapshot of a timestamp query's state and elapsed GPU time. */
class TimestampInfo {
	public final state:TimestampState;
	public final nanoseconds:haxe.Int64;

	private function new(state:TimestampState, nanoseconds:haxe.Int64) {
		this.state = state;
		this.nanoseconds = nanoseconds;
	}

	static function fromNative(value:nkgpu_timestamp_info):TimestampInfo
		return new TimestampInfo(value.get_state(), value.get_nanoseconds());
}
