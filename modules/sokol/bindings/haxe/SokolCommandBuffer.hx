import haxe.io.Bytes;

class SokolCommandBuffer {
	final bytes:Bytes;
	var length:Int;

	public function new(capacity:Int) {
		if (capacity <= 0)
			throw "command buffer capacity must be positive";
		bytes = Bytes.alloc(capacity);
		length = 0;
	}

	public function applyPipeline(pipeline:nks_pipeline):Void {
		header(1, 12);
		word(pipeline.get_id());
	}

	public function applyVertexBuffer(slot:Int, buffer:nks_buffer, offset:Int):Void {
		header(2, 20);
		word(slot);
		word(buffer.get_id());
		word(offset);
	}

	public function applyIndexBuffer(buffer:nks_buffer, offset:Int):Void {
		header(3, 16);
		word(buffer.get_id());
		word(offset);
	}

	public function applyImage(slot:Int, image:nks_image):Void {
		header(4, 16);
		word(slot);
		word(image.get_id());
	}

	public function applySampler(slot:Int, sampler:nks_sampler):Void {
		header(5, 16);
		word(slot);
		word(sampler.get_id());
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
		if (count < 0 || length > bytes.length - count)
			throw "command buffer capacity exceeded";
	}
}
