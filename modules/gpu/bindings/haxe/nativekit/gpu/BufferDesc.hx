package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;
import nativekit.gpu.Enums.BufferUsage;

/** Portable descriptor for a NativeKit GPU buffer. */
class BufferDesc {
	public var size:Int;
	public var usage:BufferUsage;
	public var dynamicUpdate:Bool;
	public var stream:Bool;

	public function new(size:Int, usage:BufferUsage) {
		this.size = size;
		this.usage = usage;
		dynamicUpdate = false;
		stream = false;
	}

	@:allow(Buffer)
	function nativeValue():nkgpu_buffer_desc {
		var value = new nkgpu_buffer_desc();
		value.set_struct_size(40);
		value.set_size(size);
		value.set_usage(usage);
		value.set_data_size(0);
		value.set_dynamic_update(dynamicUpdate ? 1 : 0);
		value.set_stream(stream ? 1 : 0);
		return value;
	}
}
