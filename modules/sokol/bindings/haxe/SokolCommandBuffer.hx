import haxe.io.Bytes;

class SokolCommandBuffer {
	var bytes:Bytes;
	var length:Int;

	public function new(capacity:Int) {
		if (capacity <= 0)
			throw "command buffer capacity must be positive";
		bytes = Bytes.alloc(capacity);
		length = 0;
	}

	public function reset():Void
		length = 0;

	public function data():Bytes
		return bytes;

	public function size():Int
		return length;

	public function applyPipeline(pipeline:nks_pipeline):Void {
		header(1, 12);
		word(pipeline.rawValue());
	}

	public function applyVertexBuffer(slot:Int, buffer:nks_buffer, offset:Int):Void {
		header(2, 20);
		word(slot);
		word(buffer.rawValue());
		word(offset);
	}

	public function applyIndexBuffer(buffer:nks_buffer, offset:Int):Void {
		header(3, 16);
		word(buffer.rawValue());
		word(offset);
	}

	public function applyImage(slot:Int, image:nks_image):Void {
		header(4, 16);
		word(slot);
		word(image.rawValue());
	}

	public function applySampler(slot:Int, sampler:nks_sampler):Void {
		header(5, 16);
		word(slot);
		word(sampler.rawValue());
	}

	public function applyUniforms(slot:Int, data:Bytes):Void {
		header(6, 16 + data.length);
		word(slot);
		word(data.length);
		require(data.length);
		for (index in 0...data.length)
			bytes.set(length + index, data.get(index));
		length += data.length;
	}

	public function applyUniform2f(slot:Int, x:Float, y:Float):Void {
		header(6, 24);
		word(slot);
		word(8);
		bytes.setFloat(length, x);
		bytes.setFloat(length + 4, y);
		length += 8;
	}

	public function draw(baseElement:Int, elementCount:Int, instanceCount:Int):Void {
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
