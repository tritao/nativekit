import haxe.io.Bytes;
import NativeKitUI;

class CanvasCommandBuffer {
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

	public function save():Void
		header(5, 8);

	public function restore():Void
		header(6, 8);

	public function transform(a:Float, b:Float, c:Float, d:Float, x:Float, y:Float):Void {
		header(1, 32);
		float(a); float(b); float(c); float(d); float(x); float(y);
	}

	public function globalAlpha(alpha:Float):Void {
		header(3, 12);
		float(alpha);
	}

	public function clipRect(x:Float, y:Float, width:Float, height:Float):Void {
		header(7, 24);
		float(x); float(y); float(width); float(height);
	}

	public function drawPath(path:nkui_resource):Void
		resource(8, path);

	public function drawImage(image:nkui_resource, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(9, image, x, y, width, height);

	public function drawText(layout:nkui_resource, x:Float, y:Float):Void
		drawRect(10, layout, x, y, 0.0, 0.0);

	public function beginLayer(opacity:Float):Void {
		header(11, 16);
		float(opacity);
		word(1);
	}

	public function endLayer():Void
		header(12, 8);

	public function drawSurface(surface:nkui_resource, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(13, surface, x, y, width, height);

	public function submit(list:nkui_display_list):Int {
		var commands = Bytes.alloc(length);
		for (index in 0...length)
			commands.set(index, bytes.get(index));
		return NativeKitUI.nkui_display_list_submit(list, commands);
	}

	function resource(opcode:Int, value:nkui_resource):Void {
		header(opcode, 12);
		word(value.get_id());
	}

	function drawRect(opcode:Int, value:nkui_resource, x:Float, y:Float, width:Float,
		height:Float):Void {
		header(opcode, 28);
		word(value.get_id());
		float(x); float(y); float(width); float(height);
	}

	function header(opcode:Int, size:Int):Void {
		require(size);
		bytes.set(length, opcode & 255);
		bytes.set(length + 1, (opcode >> 8) & 255);
		bytes.set(length + 2, 1);
		bytes.set(length + 3, 0);
		bytes.setInt32(length + 4, size);
		length += 8;
	}

	function word(value:Int):Void {
		bytes.setInt32(length, value);
		length += 4;
	}

	function float(value:Float):Void {
		bytes.setFloat(length, value);
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
