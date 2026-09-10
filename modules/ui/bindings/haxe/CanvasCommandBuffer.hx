import haxe.io.Bytes;
import NativeKitUI;
import NativeKitUI.Nkui_composite_mode;

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
		header(NativeKitUIConstants.NKUI_COMMAND_PUSH_STATE, 8);

	public function restore():Void
		header(NativeKitUIConstants.NKUI_COMMAND_POP_STATE, 8);

	public function transform(a:Float, b:Float, c:Float, d:Float, x:Float, y:Float):Void {
		header(NativeKitUIConstants.NKUI_COMMAND_SET_TRANSFORM, 32);
		float(a); float(b); float(c); float(d); float(x); float(y);
	}

	public function globalAlpha(alpha:Float):Void {
		header(NativeKitUIConstants.NKUI_COMMAND_SET_GLOBAL_ALPHA, 12);
		float(alpha);
	}

	public function paint(value:nkui_resource):Void
		resource(NativeKitUIConstants.NKUI_COMMAND_SET_PAINT, value);

	public function clipRect(x:Float, y:Float, width:Float, height:Float):Void {
		header(NativeKitUIConstants.NKUI_COMMAND_CLIP_RECT, 24);
		float(x); float(y); float(width); float(height);
	}

	public function drawPath(path:nkui_resource):Void
		resource(NativeKitUIConstants.NKUI_COMMAND_DRAW_PATH, path);

	public function drawImage(image:nkui_resource, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(NativeKitUIConstants.NKUI_COMMAND_DRAW_IMAGE, image, x, y, width, height);

	public function drawText(layout:nkui_resource, x:Float, y:Float):Void
		drawRect(NativeKitUIConstants.NKUI_COMMAND_DRAW_TEXT_LAYOUT, layout, x, y, 0.0, 0.0);

	public function beginLayer(opacity:Float):Void {
		header(NativeKitUIConstants.NKUI_COMMAND_BEGIN_LAYER, 16);
		float(opacity);
		word(Nkui_composite_mode.NKUI_COMPOSITE_SOURCE_OVER);
	}

	public function endLayer():Void
		header(NativeKitUIConstants.NKUI_COMMAND_END_LAYER, 8);

	public function drawSurface(surface:nkui_resource, x:Float, y:Float, width:Float,
		height:Float):Void
		drawRect(NativeKitUIConstants.NKUI_COMMAND_DRAW_RENDER_TARGET, surface, x, y, width, height);

	/** Submits the encoded prefix without copying it into an exact-size buffer. */
	public function submit(list:nkui_display_list):Int
		return submitRange(list, 0, length);

	/** Submits a validated range of the backing storage without copying. */
	public function submitRange(list:nkui_display_list, offset:Int, count:Int):Int {
		if (offset < 0 || offset > length || count < 0 || count > length - offset)
			throw "Command range is out of bounds";
		return NativeKitUI.nkui_display_list_submit_slice(list, bytes, offset, count);
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
		bytes.set(length + 2, NativeKitUIConstants.NKUI_COMMAND_VERSION);
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
