import haxe.io.Bytes;

/** Encodes a retained Haxe node tree into the single native frame payload. */
@:noCompletion
class LayoutTransaction {
	var nodes:Array<LayoutNode> = [];
	var parents:Array<Int> = [];
	var stringValues:Array<String> = [];
	var stringBytes:Array<Bytes> = [];
	var seen:Array<LayoutNode> = [];
	var nodeCount:Int = 0;
	var seenCount:Int = 0;
	var output:Null<Bytes>;
	var outputLength:Int = 0;

	public function new() {}

	public static function encode(root:LayoutNode):Bytes
		return new LayoutTransaction().encodeInto(root);

	/** Encodes into reusable storage and returns the backing byte buffer. */
	public function encodeInto(root:LayoutNode):Bytes {
		if (root == null)
			throw "Layout transaction requires a root node";

		nodeCount = 0;
		seenCount = 0;
		appendNode(root, -1);

		var stringByteCount = 0;
		for (index in 0...nodeCount)
			stringByteCount += this.stringBytes[index].length;
		var headerBytes:Int = NativeKitUIConstants.NKUI_LAYOUT_TRANSACTION_HEADER_BYTES;
		var recordBytes:Int = NativeKitUIConstants.NKUI_LAYOUT_NODE_RECORD_BYTES;
		var stringOffset = headerBytes + nodeCount * recordBytes;
		ensureOutput(stringOffset + stringByteCount);
		var output = this.output;
		if (output == null)
			throw "Layout transaction storage was not allocated";
		outputLength = stringOffset + stringByteCount;
		output.setInt32(0, NativeKitUIConstants.NKUI_LAYOUT_TRANSACTION_VERSION);
		output.setInt32(4, nodeCount);
		output.setInt32(8, recordBytes);
		output.setInt32(12, stringOffset);

		var textOffset = stringOffset;
		for (index in 0...nodeCount) {
			var node = nodes[index];
			var style = node.style;
			var record = headerBytes + index * recordBytes;
			var lineHeight:Float = node.paragraphStyle.lineHeight == null ? 0.0 : node.paragraphStyle.lineHeight;
			if (node.id <= 0 || node.id == 0x80000000)
				throw "Layout node ID is out of range";
			if (!finitePositive(node.textStyle.fontSize) || !finite(node.textStyle.letterSpacing) ||
				!finite(lineHeight) || lineHeight < 0.0)
				throw "Layout text style values are invalid";
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_ID_OFFSET, node.id);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PARENT_OFFSET, parents[index]);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_KIND_OFFSET, cast node.kind);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, cast style.width.sizing);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, style.width.value);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, cast style.height.sizing);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, style.height.value);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_DIRECTION_OFFSET, cast style.direction);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_LEFT_OFFSET, roundedInt(style.padding.left));
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_RIGHT_OFFSET, roundedInt(style.padding.right));
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_TOP_OFFSET, roundedInt(style.padding.top));
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_BOTTOM_OFFSET, roundedInt(style.padding.bottom));
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_CHILD_GAP_OFFSET, roundedInt(style.childGap));
			writeColor(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, style.background);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_TOP_LEFT_OFFSET, style.radiusTopLeft);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_TOP_RIGHT_OFFSET, style.radiusTopRight);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_BOTTOM_LEFT_OFFSET, style.radiusBottomLeft);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_BOTTOM_RIGHT_OFFSET, style.radiusBottomRight);
			var clip = (style.clipHorizontal ? NativeKitUIConstants.NKUI_LAYOUT_CLIP_HORIZONTAL : 0) |
				(style.clipVertical ? NativeKitUIConstants.NKUI_LAYOUT_CLIP_VERTICAL : 0);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_CLIP_FLAGS_OFFSET, clip);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, textOffset);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_LENGTH_OFFSET, this.stringBytes[index].length);
			writeColor(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_COLOR_OFFSET, node.textColor);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_FONT_FAMILY_OFFSET, cast node.textStyle.font);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, node.textStyle.fontSize);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_LETTER_SPACING_OFFSET, node.textStyle.letterSpacing);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_LINE_HEIGHT_OFFSET,
				lineHeight);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_WRAP_OFFSET, cast node.paragraphStyle.wrap);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_ALIGNMENT_OFFSET, cast node.paragraphStyle.alignment);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_DIRECTION_OFFSET, cast node.paragraphStyle.direction);
			if (textOffset < 0 || textOffset + this.stringBytes[index].length > output.length)
				throw 'Layout string table write is out of range: ${textOffset} + ${this.stringBytes[index].length} > ${output.length}';
			for (byteIndex in 0...this.stringBytes[index].length)
				output.set(textOffset + byteIndex, this.stringBytes[index].get(byteIndex));
			textOffset += this.stringBytes[index].length;
		}
		return output;
	}

	public function byteLength():Int
		return outputLength;

	function ensureOutput(required:Int):Void {
		if (required < 0)
			throw "Layout transaction size overflow";
		if (output != null && output.length >= required)
			return;
		var capacity = output == null || output.length == 0 ? required : output.length * 2;
		if (capacity < required)
			capacity = required;
		output = Bytes.alloc(capacity);
	}

	function appendNode(node:LayoutNode, parent:Int):Void {
		if (node == null || seenContains(node))
			throw "Layout tree contains a duplicate or cyclic node";
		if (seenCount == seen.length)
			seen.push(node);
		else
			seen[seenCount] = node;
		seenCount++;
		var index = nodeCount++;
		if (index == nodes.length) {
			nodes.push(node);
			parents.push(parent);
		} else {
			nodes[index] = node;
			parents[index] = parent;
		}
		var value = node.text == null ? "" : node.text;
		if (index == stringValues.length) {
			stringValues.push(value);
			stringBytes.push(Bytes.ofString(value));
		} else if (stringValues[index] != value) {
			stringValues[index] = value;
			stringBytes[index] = Bytes.ofString(value);
		}
		for (child in node.children)
			appendNode(child, index);
	}

	function seenContains(node:LayoutNode):Bool {
		for (index in 0...seenCount)
			if (seen[index] == node)
				return true;
		return false;
	}

	static function roundedInt(value:Float):Int {
		if (Math.isNaN(value) || value < 0.0 || value > 65535.0)
			throw "Layout integer value is out of range";
		return Std.int(value + 0.5);
	}

	static function finite(value:Float):Bool {
		return !Math.isNaN(value);
	}

	static function finitePositive(value:Float):Bool
		return finite(value) && value > 0.0;

	static function writeColor(output:Bytes, record:Int, offset:Int, color:Color):Void {
		writeFloat(output, record, offset, color.red);
		writeFloat(output, record, offset + 4, color.green);
		writeFloat(output, record, offset + 8, color.blue);
		writeFloat(output, record, offset + 12, color.alpha);
	}

	static function writeInt(output:Bytes, record:Int, offset:Int, value:Int):Void {
		if (record + offset < 0 || record + offset + 4 > output.length)
			throw 'Layout integer write is out of range: ${record + offset} > ${output.length}';
		else
			output.setInt32(record + offset, value);
	}

	static function writeFloat(output:Bytes, record:Int, offset:Int, value:Float):Void {
		if (record + offset < 0 || record + offset + 4 > output.length)
			throw 'Layout float write is out of range: ${record + offset} > ${output.length}';
		else
			output.setFloat(record + offset, value);
	}
}
