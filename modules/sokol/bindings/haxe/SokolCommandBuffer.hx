import haxe.io.Bytes;

class SokolCommandBuffer {
	final renderer:SokolRenderer;
	var bytes:Bytes;
	var length:Int;

	@:allow(SokolRenderer)
	private function new(renderer:SokolRenderer, capacity:Int) {
		if (capacity <= 0)
			throw "command buffer capacity must be positive";
		this.renderer = renderer;
		bytes = Bytes.alloc(capacity);
		length = 0;
	}

	public function reset():Void
		length = 0;

	public function data():Bytes
		return bytes;

	public function size():Int
		return length;

	@:allow(SokolRenderer)
	function ensureRenderer(owner:SokolRenderer):Void {
		if (renderer != owner)
			throw "Sokol command buffer belongs to a different renderer";
	}

	public function applyPipeline(pipeline:SokolPipeline):Void {
		ensureRenderer(pipeline.rendererOwner());
		header(1, 12);
		word(pipeline.nativeHandle().rawValue());
	}

	public function applyVertexBuffer(slot:Int, buffer:SokolBuffer, offset:Int):Void {
		ensureRenderer(buffer.rendererOwner());
		if (slot < 0 || offset < 0)
			throw "Sokol vertex-buffer slot and offset must be non-negative";
		header(2, 20);
		word(slot);
		word(buffer.nativeHandle().rawValue());
		word(offset);
	}

	public function applyIndexBuffer(buffer:SokolBuffer, offset:Int):Void {
		ensureRenderer(buffer.rendererOwner());
		if (offset < 0)
			throw "Sokol index-buffer offset must be non-negative";
		header(3, 16);
		word(buffer.nativeHandle().rawValue());
		word(offset);
	}

	public function applyImage(slot:Int, image:SokolImage):Void {
		ensureRenderer(image.rendererOwner());
		if (slot < 0)
			throw "Sokol image slot must be non-negative";
		header(4, 16);
		word(slot);
		word(image.nativeHandle().rawValue());
	}

	public function applySampler(slot:Int, sampler:SokolSampler):Void {
		ensureRenderer(sampler.rendererOwner());
		if (slot < 0)
			throw "Sokol sampler slot must be non-negative";
		header(5, 16);
		word(slot);
		word(sampler.nativeHandle().rawValue());
	}

	public function applyUniforms(slot:Int, data:Bytes):Void {
		if (slot < 0 || data == null)
			throw "Sokol uniform slot and data are invalid";
		header(6, 16 + data.length);
		word(slot);
		word(data.length);
		require(data.length);
		for (index in 0...data.length)
			bytes.set(length + index, data.get(index));
		length += data.length;
	}

	public function applyUniform2f(slot:Int, x:Float, y:Float):Void {
		if (slot < 0)
			throw "Sokol uniform slot must be non-negative";
		header(6, 24);
		word(slot);
		word(8);
		bytes.setFloat(length, x);
		bytes.setFloat(length + 4, y);
		length += 8;
	}

	public function draw(baseElement:Int, elementCount:Int, instanceCount:Int):Void {
		if (baseElement < 0 || elementCount <= 0 || instanceCount <= 0)
			throw "Sokol draw range and instance count must be positive";
		header(7, 20);
		word(baseElement);
		word(elementCount);
		word(instanceCount);
	}

	public function finish():Bytes
		return bytes.sub(0, length);

	function header(opcode:Int, size:Int):Void {
		require(size);
		word(opcode);
		word(size);
	}

	function word(value:Int):Void {
		bytes.setInt32(length, value);
		length += 4;
	}

	function require(count:Int):Void {
		if (count < 0)
			throw "negative command size";
		if (length <= bytes.length - count)
			return;
		var capacity = bytes.length;
		while (capacity < length + count)
			capacity *= 2;
		var grown = Bytes.alloc(capacity);
		for (index in 0...length)
			grown.set(index, bytes.get(index));
		bytes = grown;
	}
}
