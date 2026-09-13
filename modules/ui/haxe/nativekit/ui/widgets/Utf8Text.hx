package nativekit.ui.widgets;

import haxe.io.Bytes;

/** UTF-8 helpers using the code-point offsets used by NativeKit text APIs. */
class Utf8Text {
	public static function length(text:String):Int {
		if (text == null)
			return 0;
		var bytes = Bytes.ofString(text);
		var count = 0;
		var offset = 0;
		while (offset < bytes.length) {
			offset = nextOffset(bytes, offset);
			count++;
		}
		return count;
	}

	public static function slice(text:String, start:Int, end:Int):String {
		var bytes = Bytes.ofString(text == null ? "" : text);
		var count = codePointLength(bytes);
		if (start < 0 || end < start || end > count)
			throw "UTF-8 slice is outside the string";
		var byteStart = byteOffset(bytes, start);
		var byteEnd = byteOffset(bytes, end);
		return bytes.sub(byteStart, byteEnd - byteStart).toString();
	}

	public static function replace(text:String, start:Int, end:Int, replacement:Null<String>):String {
		var source = Bytes.ofString(text == null ? "" : text);
		var insert = Bytes.ofString(replacement == null ? "" : replacement);
		var count = codePointLength(source);
		if (start < 0 || end < start || end > count)
			throw "UTF-8 replacement range is outside the string";
		var byteStart = byteOffset(source, start);
		var byteEnd = byteOffset(source, end);
		var output = Bytes.alloc(byteStart + insert.length + source.length - byteEnd);
		var cursor = 0;
		for (index in 0...byteStart)
			output.set(cursor++, source.get(index));
		for (index in 0...insert.length)
			output.set(cursor++, insert.get(index));
		for (index in byteEnd...source.length)
			output.set(cursor++, source.get(index));
		return output.toString();
	}

	static function codePointLength(bytes:Bytes):Int {
		var count = 0;
		var offset = 0;
		while (offset < bytes.length) {
			offset = nextOffset(bytes, offset);
			count++;
		}
		return count;
	}

	static function byteOffset(bytes:Bytes, codePoint:Int):Int {
		var offset = 0;
		for (_ in 0...codePoint)
			offset = nextOffset(bytes, offset);
		return offset;
	}

	static function nextOffset(bytes:Bytes, offset:Int):Int {
		var first = bytes.get(offset);
		var count = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
			first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
		if (count == 0 || offset > bytes.length - count)
			throw "Invalid UTF-8 text";
		for (index in 1...count)
			if (bytes.get(offset + index) < 0x80 || bytes.get(offset + index) > 0xbf)
				throw "Invalid UTF-8 text";
		if (count >= 3) {
			var second = bytes.get(offset + 1);
			if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
				(first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
				throw "Invalid UTF-8 text";
		}
		return offset + count;
	}
}
