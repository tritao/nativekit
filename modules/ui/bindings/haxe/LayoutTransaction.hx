import haxe.io.Bytes;

/** Encodes a retained Haxe node tree into the single native frame payload. */
@:noCompletion
class LayoutTransaction {
	static inline var HEADER_BYTES:Int = 16;
	static inline var RECORD_BYTES:Int = 128;

	public static function encode(root:LayoutNode):Bytes {
		if (root == null)
			throw "Layout transaction requires a root node";

		var nodes:Array<LayoutNode> = [];
		var parents:Array<Int> = [];
		var strings:Array<Bytes> = [];
		var seen:Array<LayoutNode> = [];
		appendNode(root, -1, nodes, parents, strings, seen);

		var stringBytes = 0;
		for (value in strings)
			stringBytes += value.length;
		var stringOffset = HEADER_BYTES + nodes.length * RECORD_BYTES;
		var output = Bytes.alloc(stringOffset + stringBytes);
		output.setInt32(0, NativeKitUIConstants.NKUI_LAYOUT_TRANSACTION_VERSION);
		output.setInt32(4, nodes.length);
		output.setInt32(8, RECORD_BYTES);
		output.setInt32(12, stringOffset);

		var textOffset = stringOffset;
		for (index in 0...nodes.length) {
			var node = nodes[index];
			var style = node.style;
			var record = HEADER_BYTES + index * RECORD_BYTES;
			if (node.id <= 0 || node.id == 0x80000000)
				throw "Layout node ID is out of range";
			if (node.fontId < 0 || node.fontId > 65535 || node.fontSize < 0 || node.fontSize > 65535 ||
				node.lineHeight < 0 || node.lineHeight > 65535 || node.letterSpacing < 0 || node.letterSpacing > 65535)
				throw "Layout text value is out of range";
			writeInt(output, record, 0, node.id);
			writeInt(output, record, 4, parents[index]);
			writeInt(output, record, 8, cast node.kind);
			writeInt(output, record, 12, cast style.width.sizing);
			writeFloat(output, record, 16, style.width.value);
			writeInt(output, record, 20, cast style.height.sizing);
			writeFloat(output, record, 24, style.height.value);
			writeInt(output, record, 28, cast style.direction);
			writeInt(output, record, 32, roundedInt(style.padding.left));
			writeInt(output, record, 36, roundedInt(style.padding.right));
			writeInt(output, record, 40, roundedInt(style.padding.top));
			writeInt(output, record, 44, roundedInt(style.padding.bottom));
			writeInt(output, record, 48, roundedInt(style.childGap));
			writeColor(output, record, 52, style.background);
			writeFloat(output, record, 68, style.radiusTopLeft);
			writeFloat(output, record, 72, style.radiusTopRight);
			writeFloat(output, record, 76, style.radiusBottomLeft);
			writeFloat(output, record, 80, style.radiusBottomRight);
			var clip = (style.clipHorizontal ? NativeKitUIConstants.NKUI_LAYOUT_CLIP_HORIZONTAL : 0) |
				(style.clipVertical ? NativeKitUIConstants.NKUI_LAYOUT_CLIP_VERTICAL : 0);
			writeInt(output, record, 84, clip);
			writeInt(output, record, 88, textOffset);
			writeInt(output, record, 92, strings[index].length);
			writeColor(output, record, 96, node.textColor);
			writeInt(output, record, 112, node.fontId);
			writeInt(output, record, 116, node.fontSize);
			writeInt(output, record, 120, node.lineHeight);
			writeInt(output, record, 124, node.letterSpacing);
			for (byteIndex in 0...strings[index].length)
				output.set(textOffset + byteIndex, strings[index].get(byteIndex));
			textOffset += strings[index].length;
		}
		return output;
	}

	static function appendNode(node:LayoutNode, parent:Int, nodes:Array<LayoutNode>, parents:Array<Int>,
			strings:Array<Bytes>, seen:Array<LayoutNode>):Void {
		if (node == null || seen.indexOf(node) >= 0)
			throw "Layout tree contains a duplicate or cyclic node";
		seen.push(node);
		var index = nodes.length;
		nodes.push(node);
		parents.push(parent);
		strings.push(Bytes.ofString(node.text == null ? "" : node.text));
		for (child in node.children)
			appendNode(child, index, nodes, parents, strings, seen);
	}

	static function roundedInt(value:Float):Int {
		if (Math.isNaN(value) || value < 0.0 || value > 65535.0)
			throw "Layout integer value is out of range";
		return Std.int(value + 0.5);
	}

	static function writeColor(output:Bytes, record:Int, offset:Int, color:Color):Void {
		writeFloat(output, record, offset, color.red);
		writeFloat(output, record, offset + 4, color.green);
		writeFloat(output, record, offset + 8, color.blue);
		writeFloat(output, record, offset + 12, color.alpha);
	}

	static function writeInt(output:Bytes, record:Int, offset:Int, value:Int):Void
		output.setInt32(record + offset, value);

	static function writeFloat(output:Bytes, record:Int, offset:Int, value:Float):Void
		output.setFloat(record + offset, value);
}
