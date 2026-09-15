import haxe.io.Bytes;
import NativeKitUI.NkuiLayoutClipFlags;

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
			var measureVersion = node.measureVersion;
			if (node.intrinsicContent != null)
				measureVersion = node.intrinsicContent.getVersion();
			var lineHeight:Float = node.paragraphStyle.lineHeight == null ? 0.0 : node.paragraphStyle.lineHeight;
			if (node.id <= 0 || node.id == 0x80000000)
				throw "Layout node ID is out of range";
			if (measureVersion < 0)
				throw "Layout measurement version must be non-negative";
			if (!finitePositive(node.textStyle.fontSize) || !finite(node.textStyle.letterSpacing) ||
				!finite(lineHeight) || lineHeight < 0.0)
				throw "Layout text style values are invalid";
			if (!validAxis(style.width) || !validAxis(style.height) || !finite(style.aspectRatio) ||
				style.aspectRatio < 0.0)
				throw "Layout sizing values are invalid";
			var determinant = style.transform.a * style.transform.d -
				style.transform.b * style.transform.c;
			var childAlignX:Int = style.childAlignX;
			var childAlignY:Int = style.childAlignY;
			var childDistribution:Int = style.childDistribution;
			var positioning:Int = style.positioning;
			var wrapMode:Int = style.wrapMode;
			var alignSelf:Int = style.alignSelf;
			if (childAlignX < LayoutAlignmentX.Start || childAlignX > LayoutAlignmentX.Center ||
				childAlignY < LayoutAlignmentY.Start || childAlignY > LayoutAlignmentY.Baseline ||
				childDistribution < LayoutDistribution.Start ||
				childDistribution > LayoutDistribution.SpaceEvenly ||
				wrapMode < LayoutWrapMode.NoWrap || wrapMode > LayoutWrapMode.Wrap ||
				alignSelf < LayoutSelfAlignment.Inherit || alignSelf > LayoutSelfAlignment.Baseline ||
				(positioning != LayoutPositioning.Flow && positioning != LayoutPositioning.Absolute) ||
				!validSpacing(style.padding.left) || !validSpacing(style.padding.right) ||
				!validSpacing(style.padding.top) || !validSpacing(style.padding.bottom) ||
				!validSpacing(style.childGap) || !validSpacing(style.rowGap) ||
				!validSpacing(style.columnGap) ||
				!finite(style.positionX) || !finite(style.positionY) ||
				style.zIndex < -32768 || style.zIndex > 32767)
				throw "Layout alignment or positioning is invalid";
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_ID_OFFSET, node.id);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PARENT_OFFSET, parents[index]);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET,
				cast node.visualKind);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, cast style.width.sizing);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, style.width.value);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, cast style.height.sizing);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, style.height.value);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_MIN_OFFSET, style.width.min);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_MAX_OFFSET, style.width.max);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_MIN_OFFSET, style.height.min);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_MAX_OFFSET, style.height.max);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_ASPECT_RATIO_OFFSET, style.aspectRatio);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WIDTH_GROW_WEIGHT_OFFSET,
				style.width.growWeight);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_HEIGHT_GROW_WEIGHT_OFFSET,
				style.height.growWeight);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_DIRECTION_OFFSET, cast style.direction);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_LEFT_OFFSET, style.padding.left);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_RIGHT_OFFSET, style.padding.right);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_TOP_OFFSET, style.padding.top);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_PADDING_BOTTOM_OFFSET, style.padding.bottom);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_CHILD_GAP_OFFSET, style.childGap);
			writeColor(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, style.background);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_TOP_LEFT_OFFSET, style.radiusTopLeft);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_TOP_RIGHT_OFFSET, style.radiusTopRight);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_BOTTOM_LEFT_OFFSET, style.radiusBottomLeft);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_RADIUS_BOTTOM_RIGHT_OFFSET, style.radiusBottomRight);
			var clip = (style.clipHorizontal ? NkuiLayoutClipFlags.Horizontal : 0) |
				(style.clipVertical ? NkuiLayoutClipFlags.Vertical : 0);
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
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TEXT_FLAGS_OFFSET, 0);
			var transform = style.transform;
			if (!finite(transform.a) || !finite(transform.b) || !finite(transform.c) ||
				!finite(transform.d) || !finite(transform.tx) || !finite(transform.ty) ||
				(determinant > -0.000001 && determinant < 0.000001))
				throw "Layout transform must be finite and invertible";
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET, transform.a);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_B_OFFSET, transform.b);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_C_OFFSET, transform.c);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET, transform.d);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_TX_OFFSET, transform.tx);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_TRANSFORM_TY_OFFSET, transform.ty);
			var nodeFlags = style.visible ? 1 : 0;
			if (positioning == LayoutPositioning.Absolute) {
				nodeFlags |= 1 << 1;
				if (style.clipToParent)
					nodeFlags |= 1 << 2;
			}
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_FLAGS_OFFSET, nodeFlags);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_CHILD_ALIGNMENT_OFFSET,
				childAlignX | (childAlignY << 8));
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_CHILD_DISTRIBUTION_OFFSET,
				childDistribution);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_ROW_GAP_OFFSET,
				style.rowGap);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_COLUMN_GAP_OFFSET,
				style.columnGap);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_WRAP_MODE_OFFSET,
				wrapMode);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_ALIGN_SELF_OFFSET,
				alignSelf);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_MEASURE_VERSION_OFFSET,
				measureVersion);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_POSITION_X_OFFSET,
				style.positionX);
			writeFloat(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_POSITION_Y_OFFSET,
				style.positionY);
			writeInt(output, record, NativeKitUIConstants.NKUI_LAYOUT_NODE_Z_INDEX_OFFSET,
				style.zIndex);
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

	static function finite(value:Float):Bool {
		return value == value && value - value == 0.0;
	}

	static function validSpacing(value:Float):Bool {
		return finite(value) && value >= 0.0;
	}

	static function finitePositive(value:Float):Bool
		return finite(value) && value > 0.0;

	static function validAxis(axis:LayoutAxis):Bool {
		var sizing:Int = axis == null ? -1 : cast axis.sizing;
		if (axis == null || sizing < LayoutSizing.Fit || sizing > LayoutSizing.Percent ||
			!finite(axis.value) || axis.value < 0.0 || !finite(axis.min) ||
			axis.min < 0.0 || !finite(axis.max) || axis.max < 0.0 ||
			!finite(axis.growWeight) || axis.growWeight <= 0.0)
			return false;
		if (axis.sizing == LayoutSizing.Percent)
			return axis.value <= 1.0 && axis.min == 0.0 && axis.max == 0.0 &&
				axis.growWeight == 1.0;
		if (axis.sizing == LayoutSizing.Fixed)
			return axis.min == 0.0 && axis.max == 0.0 && axis.growWeight == 1.0;
		if (axis.sizing != LayoutSizing.Grow && axis.growWeight != 1.0)
			return false;
		return axis.max == 0.0 || axis.max >= axis.min;
	}

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
