import haxe.io.Bytes;
import SokolEnums.SokolBufferUsage;

/** Renderer-owned GPU buffer; its handle cannot be used after disposal. */
class SokolBuffer {
	final renderer:SokolRenderer;
	final value:nks_buffer;
	var disposed:Bool = false;

	private function new(renderer:SokolRenderer, value:nks_buffer) {
		this.renderer = renderer;
		this.value = value;
		renderer.registerResource(rendererClosed);
	}

	public static function fromBytes(renderer:SokolRenderer, data:Bytes):SokolBuffer {
		if (data == null || data.length == 0)
			throw "Sokol buffer data must not be empty";
		var made = NativeKitSokol.nks_buffer_create(renderer.nativeHandle(), data, data.length);
		SokolResult.check(made.status, "buffer.create");
		return new SokolBuffer(renderer, made.out_buffer);
	}

	public static function vertexFloats(renderer:SokolRenderer, values:Array<Float>):SokolBuffer {
		if (values == null || values.length == 0 || values.length > 536870911)
			throw "Sokol vertex buffer requires a non-empty, bounded float array";
		var madeBuilder = NativeKitSokol.nks_buffer_begin_kind(renderer.nativeHandle(), values.length * 4, SokolBufferUsage.Vertex);
		SokolResult.check(madeBuilder.status, "buffer.beginVertex");
		for (index in 0...values.length)
			SokolResult.check(NativeKitSokol.nks_buffer_write_f32(madeBuilder.out_builder, index * 4, values[index]), "buffer.writeFloat");
		var made = NativeKitSokol.nks_buffer_end(madeBuilder.out_builder);
		SokolResult.check(made.status, "buffer.endVertex");
		return new SokolBuffer(renderer, made.out_buffer);
	}

	public static function indices16(renderer:SokolRenderer, values:Array<Int>):SokolBuffer {
		if (values == null || values.length == 0 || values.length > 1073741823)
			throw "Sokol index buffer requires a non-empty, bounded index array";
		var madeBuilder = NativeKitSokol.nks_buffer_begin_kind(renderer.nativeHandle(), values.length * 2, SokolBufferUsage.Index);
		SokolResult.check(madeBuilder.status, "buffer.beginIndex");
		for (index in 0...values.length)
			SokolResult.check(NativeKitSokol.nks_buffer_write_u16(madeBuilder.out_builder, index * 2, values[index]), "buffer.writeIndex");
		var made = NativeKitSokol.nks_buffer_end(madeBuilder.out_builder);
		SokolResult.check(made.status, "buffer.endIndex");
		return new SokolBuffer(renderer, made.out_buffer);
	}

	public function nativeHandle():nks_buffer {
		ensureLive();
		return value;
	}

	public function applyVertex(slot:Int, offset:Int = 0):Void {
		ensureLive();
		renderer.ensureFrame();
		if (slot < 0 || offset < 0)
			throw "Sokol vertex-buffer slot and offset must be non-negative";
		SokolResult.check(NativeKitSokol.nks_apply_vertex_buffer(renderer.nativeHandle(), slot, value, offset), "buffer.applyVertex");
	}

	public function applyIndex(offset:Int = 0):Void {
		ensureLive();
		renderer.ensureFrame();
		if (offset < 0)
			throw "Sokol index-buffer offset must be non-negative";
		SokolResult.check(NativeKitSokol.nks_apply_index_buffer(renderer.nativeHandle(), value, offset), "buffer.applyIndex");
	}

	public function dispose():Void {
		if (disposed)
			return;
		renderer.ensureResourceOperation();
		SokolResult.check(NativeKitSokol.nks_buffer_destroy(renderer.nativeHandle(), value), "buffer.dispose");
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	@:allow(SokolCommandBuffer)
	function rendererOwner():SokolRenderer
		return renderer;

	@:allow(SokolRenderer)
	function rendererClosed():Void
		disposed = true;

	function ensureLive():Void {
		if (disposed)
			throw "Sokol buffer has been disposed";
		renderer.ensureResourceOperation();
	}
}
