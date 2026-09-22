package nativekit.gpu;

import nativekit.ffi.NativeKitGpu;
import haxe.io.Bytes;
import nativekit.gpu.Enums.ReadbackState;

/** Owns one asynchronous image or buffer readback request. */
class Readback {
	final renderer:Renderer;
	final value:nkgpu_readback;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_readback) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	/** Starts a tightly packed readback of an image rectangle. */
	public static function begin(image:Image, x:Int, y:Int, width:Int, height:Int,
		mipLevel:Int = 0, layer:Int = 0):Readback {
		if (image == null || width <= 0 || height <= 0 || x < 0 || y < 0 ||
			mipLevel < 0 || layer < 0)
			throw "GPU readback region is invalid";
		var renderer = image.rendererOwner();
		renderer.ensureResourceOperation();
		var desc = new nkgpu_image_readback_desc();
		desc.set_struct_size(32);
		desc.set_image(image.nativeHandle());
		desc.set_mip_level(mipLevel);
		desc.set_layer(layer);
		desc.set_x(x);
		desc.set_y(y);
		desc.set_width(width);
		desc.set_height(height);
		var made = NativeKitGpu.nkgpu_readback_begin_image(renderer.nativeHandle(), desc);
		GpuResult.check(made.status, "readback.begin");
		return new Readback(renderer, made.out_readback);
	}

	/** Starts a tightly packed readback of an arbitrary buffer range. */
	public static function beginBuffer(buffer:Buffer, offset:Int, size:Int):Readback {
		if (buffer == null || offset < 0 || size <= 0)
			throw "GPU buffer readback range is invalid";
		var renderer = buffer.rendererOwner();
		renderer.ensureResourceOperation();
		var desc = new nkgpu_buffer_readback_desc();
		desc.set_struct_size(16);
		desc.set_buffer(buffer.nativeHandle());
		desc.set_offset(offset);
		desc.set_size(size);
		var made = NativeKitGpu.nkgpu_readback_begin_buffer(renderer.nativeHandle(), desc);
		GpuResult.check(made.status, "readback.beginBuffer");
		return new Readback(renderer, made.out_readback);
	}

	public function query():ReadbackInfo {
		ensureLive();
		renderer.ensureResourceOperation();
		var result = NativeKitGpu.nkgpu_readback_query(renderer.nativeHandle(), value);
		GpuResult.check(result.status, "readback.query");
		return ReadbackInfo.fromNative(result.out_info);
	}

	public function read(data:Bytes):Int {
		ensureLive();
		if (data == null || data.length == 0)
			throw "GPU readback destination must not be empty";
		renderer.ensureResourceOperation();
		var result = NativeKitGpu.nkgpu_readback_read(renderer.nativeHandle(), value, data, data.length);
		GpuResult.check(result.status, "readback.read");
		return result.out_size;
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_readback_destroy(renderer.nativeHandle(), value), "readback.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "GPU readback has been disposed";
		renderer.ensureLive();
	}
}

/** Snapshot of an asynchronous readback's state and packed result layout. */
class ReadbackInfo {
	public final state:ReadbackState;
	public final size:Int;
	public final rowPitch:Int;
	public final width:Int;
	public final height:Int;

	private function new(state:ReadbackState, size:Int, rowPitch:Int, width:Int, height:Int) {
		this.state = state;
		this.size = size;
		this.rowPitch = rowPitch;
		this.width = width;
		this.height = height;
	}

	static function fromNative(value:nkgpu_readback_info):ReadbackInfo
		return new ReadbackInfo(value.get_state(), value.get_size(), value.get_row_pitch(),
			value.get_width(), value.get_height());
}
