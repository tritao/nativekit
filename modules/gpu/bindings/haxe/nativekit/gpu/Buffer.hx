package nativekit.gpu;

import haxe.io.Bytes;
import nativekit.gpu.Enums.BufferUsage;

/** Renderer-owned GPU buffer; its handle cannot be used after disposal. */
class Buffer {
	final renderer:Renderer;
	final value:nkgpu_buffer;
	var disposed:Bool = false;

	private function new(renderer:Renderer, value:nkgpu_buffer) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	/** Creates a buffer from the current generic buffer descriptor. */
	public static function create(renderer:Renderer, desc:BufferDesc):Buffer {
		if (desc == null || desc.size <= 0)
			throw "GPU buffer descriptor size must be positive";
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_buffer_create_desc(renderer.nativeHandle(), desc.nativeValue());
		GpuResult.check(made.status, "buffer.create");
		return new Buffer(renderer, made.out_buffer);
	}

	public static function fromBytes(renderer:Renderer, data:Bytes):Buffer {
		if (data == null || data.length == 0)
			throw "GPU buffer data must not be empty";
		renderer.ensureResourceOperation();
		var made = NativeKitGpu.nkgpu_buffer_create(renderer.nativeHandle(), data, data.length);
		GpuResult.check(made.status, "buffer.create");
		return new Buffer(renderer, made.out_buffer);
	}

	public static function vertexFloats(renderer:Renderer, values:Array<Float>):Buffer {
		if (values == null || values.length == 0 || values.length > 536870911)
			throw "GPU vertex buffer requires a non-empty, bounded float array";
		renderer.ensureResourceOperation();
		var madeBuilder = NativeKitGpu.nkgpu_buffer_begin_kind(renderer.nativeHandle(), values.length * 4, BufferUsage.Vertex);
		GpuResult.check(madeBuilder.status, "buffer.beginVertex");
		for (index in 0...values.length)
			GpuResult.check(NativeKitGpu.nkgpu_buffer_write_f32(madeBuilder.out_builder, index * 4, values[index]), "buffer.writeFloat");
		var made = NativeKitGpu.nkgpu_buffer_end(madeBuilder.out_builder);
		GpuResult.check(made.status, "buffer.endVertex");
		return new Buffer(renderer, made.out_buffer);
	}

	public static function indices16(renderer:Renderer, values:Array<Int>):Buffer {
		if (values == null || values.length == 0 || values.length > 1073741823)
			throw "GPU index buffer requires a non-empty, bounded index array";
		renderer.ensureResourceOperation();
		var madeBuilder = NativeKitGpu.nkgpu_buffer_begin_kind(renderer.nativeHandle(), values.length * 2, BufferUsage.Index);
		GpuResult.check(madeBuilder.status, "buffer.beginIndex");
		for (index in 0...values.length)
			GpuResult.check(NativeKitGpu.nkgpu_buffer_write_u16(madeBuilder.out_builder, index * 2, values[index]), "buffer.writeIndex");
		var made = NativeKitGpu.nkgpu_buffer_end(madeBuilder.out_builder);
		GpuResult.check(made.status, "buffer.endIndex");
		return new Buffer(renderer, made.out_buffer);
	}

	public function nativeHandle():nkgpu_buffer {
		ensureLive();
		return value;
	}

	public function applyVertex(slot:Int, offset:Int = 0):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0 || offset < 0)
			throw "GPU vertex-buffer slot and offset must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_vertex_buffer(renderer.nativeHandle(), slot, value, offset), "buffer.applyVertex");
	}

	public function applyIndex(offset:Int = 0):Void {
		ensureLive();
		renderer.ensureFrame();
		if (offset < 0)
			throw "GPU index-buffer offset must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_index_buffer(renderer.nativeHandle(), value, offset), "buffer.applyIndex");
	}

	/** Binds this buffer as a storage resource in the active pass. */
	public function applyStorage(slot:Int):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0)
			throw "GPU storage-buffer slot must be non-negative";
		GpuResult.check(NativeKitGpu.nkgpu_apply_storage_buffer(renderer.nativeHandle(), slot, value),
			"buffer.applyStorage");
	}

	/** Updates an arbitrary byte range of a dynamic buffer. */
	public function update(offset:Int, data:Bytes):Void {
		ensureLive();
		if (offset < 0 || data == null || data.length == 0)
			throw "GPU buffer update arguments are invalid";
		GpuResult.check(NativeKitGpu.nkgpu_buffer_update(renderer.nativeHandle(), value, offset, data,
			data.length), "buffer.update");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		GpuResult.check(NativeKitGpu.nkgpu_buffer_destroy(renderer.nativeHandle(), value), "buffer.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(CommandBuffer, Renderer)
	function rendererOwner():Renderer
		return renderer;

	@:allow(Renderer)
	function rendererClosed():Void
		disposed = true;

	@:allow(Renderer)
	function ensureLive():Void {
		if (disposed)
			throw "GPU buffer has been disposed";
		renderer.ensureLive();
	}
}
