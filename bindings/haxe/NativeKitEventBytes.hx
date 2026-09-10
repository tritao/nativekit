import NativeKitEventValue.NativeKitResource;

/** Validates and decodes byte-oriented NativeKit event payloads. */
class NativeKitEventBytes {
	public static function requireSize(data:haxe.io.Bytes, size:Int):Void
		if (data.length != size) throw "NativeKit event payload has an invalid size";

	public static function requireMinimumSize(data:haxe.io.Bytes, size:Int):Void
		if (size < 0 || data.length < size) throw "NativeKit event payload is truncated";

	public static function readU32(data:haxe.io.Bytes, offset:Int):Int {
		if (offset < 0 || offset > data.length - 4)
			throw "NativeKit event payload has a truncated integer";
		return data.get(offset) | data.get(offset + 1) << 8 |
			data.get(offset + 2) << 16 | data.get(offset + 3) << 24;
	}

	public static function decodeClipboardFiles(data:haxe.io.Bytes, expectedCount:Int):Array<String>
		return decodePackedStrings(data, expectedCount, 8);

	public static function decodeDropItems(data:haxe.io.Bytes, expectedCount:Int):Array<String>
		return decodePackedStrings(data, expectedCount, 16);

	public static function decodeDialogPaths(data:haxe.io.Bytes):Array<String> {
		requireMinimumSize(data, 16);
		var count = readU32(data, 4), offsets = readU32(data, 8), strings = readU32(data, 12);
		if (offsets < 16 || strings < offsets || strings > data.length || count > Std.int((strings - offsets) / 4))
			throw "NativeKit dialog payload has an invalid offset table";
		var values:Array<String> = [];
		for (index in 0...count)
			values.push(requireString(data, readU32(data, offsets + index * 4), strings));
		return values;
	}

	public static function decodeResourceList(data:haxe.io.Bytes, listOffset:Int):{accepted:Bool, items:Array<NativeKitResource>} {
		if (listOffset < 0 || listOffset > data.length - 16)
			throw "NativeKit resource payload has an invalid list offset";
		var accepted = readU32(data, listOffset) != 0;
		var count = readU32(data, listOffset + 4), items = readU32(data, listOffset + 8), strings = readU32(data, listOffset + 12);
		if (items < listOffset + 16 || strings < items || strings > data.length || count > Std.int((strings - items) / 16))
			throw "NativeKit resource payload has an invalid table";
		var values:Array<NativeKitResource> = [];
		for (index in 0...count) {
			var base = items + index * 16;
			var uri = readOptionalString(data, readU32(data, base + 4), strings);
			if (uri == null || uri.length == 0) throw "NativeKit resource payload has an invalid URI";
			values.push(new NativeKitResource(readU32(data, base), uri,
				readOptionalString(data, readU32(data, base + 8), strings),
				readOptionalString(data, readU32(data, base + 12), strings)));
		}
		return {accepted: accepted, items: values};
	}

	public static function readOptionalString(data:haxe.io.Bytes, offset:Int, minimum:Int):Null<String> {
		if (offset == 0) return null;
		return requireString(data, offset, minimum);
	}

	static function decodePackedStrings(data:haxe.io.Bytes, expectedCount:Int, headerSize:Int):Array<String> {
		requireMinimumSize(data, headerSize);
		var count = readU32(data, headerSize == 8 ? 0 : 8);
		var strings = readU32(data, headerSize == 8 ? 4 : 12);
		if (count != expectedCount || strings < headerSize || strings > data.length)
			throw "NativeKit packed event payload has an invalid header";
		var values:Array<String> = [], cursor = strings;
		for (_ in 0...count) {
			var end = cursor;
			while (end < data.length && data.get(end) != 0) end++;
			if (end >= data.length) throw "NativeKit event payload has an unterminated string";
			values.push(data.getString(cursor, end - cursor));
			cursor = end + 1;
		}
		return values;
	}

	static function requireString(data:haxe.io.Bytes, offset:Int, minimum:Int):String {
		if (offset < minimum || offset >= data.length)
			throw "NativeKit event payload has an invalid string offset";
		var end = offset;
		while (end < data.length && data.get(end) != 0) end++;
		if (end >= data.length) throw "NativeKit event payload has an unterminated string";
		return data.getString(offset, end - offset);
	}
}
