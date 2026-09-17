package nativekit.ui.widgets;

import haxe.io.Bytes;

/**
 * Cached coordinate conversions for one UTF-8 string, normally one active
 * paragraph. All conversion methods use the coordinate name in their API so a
 * byte offset cannot be accidentally passed as a UTF-16 or code-point offset.
 *
 * The generated grapheme boundaries cover the common Unicode rules needed by
 * editor movement (combining marks, emoji modifiers, ZWJ sequences, regional
 * indicator flags, Hangul, and Indic viramas). A shaping backend may replace
 * them with its authoritative boundaries using setGraphemeBoundaries().
 */
class TextOffsetMap {
	public var text(default, null):String;
	public var codepointCount(default, null):Int;
	public var utf8ByteLength(default, null):Int;
	public var utf16Length(default, null):Int;
	var codepointToUtf8:Array<Int>;
	var codepointToUtf16:Array<Int>;
	var codepoints:Array<Int>;
	/** Encoded document bytes reused by paragraph slicing and replacement queries. */
	var utf8Bytes:Bytes;
	var paragraphStarts:Array<Int>;
	var paragraphEnds:Array<Int>;
	var graphemeBoundaries:Array<Int>;

	public function new(value:String, ?boundaries:Array<Int>) {
		text = value == null ? "" : value;
		utf8Bytes = Bytes.ofString(text);
		codepointToUtf8 = [0];
		codepointToUtf16 = [0];
		codepoints = [];
		var byteOffset = 0;
		var utf16Offset = 0;
		while (byteOffset < utf8Bytes.length) {
			var codepoint = decodeCodepoint(utf8Bytes, byteOffset);
			codepoints.push(codepoint);
			byteOffset = nextOffset(utf8Bytes, byteOffset);
			utf16Offset += codepoint > 0xffff ? 2 : 1;
			codepointToUtf8.push(byteOffset);
			codepointToUtf16.push(utf16Offset);
		}
		codepointCount = codepoints.length;
		utf8ByteLength = utf8Bytes.length;
		utf16Length = utf16Offset;
		paragraphStarts = [0];
		paragraphEnds = [];
		for (codepointOffset in 0...codepoints.length)
			if (codepoints[codepointOffset] == 0x0a) {
				paragraphEnds.push(codepointOffset);
				paragraphStarts.push(codepointOffset + 1);
			}
		paragraphEnds.push(codepointCount);
		graphemeBoundaries = buildGraphemeBoundaries();
		if (boundaries != null)
			setGraphemeBoundaries(boundaries);
	}

	/** Counts Unicode scalar values without retaining a second mapping cache. */
	public static function countCodepoints(value:String):Int {
		if (value == null || value.length == 0)
			return 0;
		var bytes = Bytes.ofString(value);
		var count = 0;
		var byteOffset = 0;
		while (byteOffset < bytes.length) {
			byteOffset = nextOffset(bytes, byteOffset);
			count++;
		}
		return count;
	}

	public function utf8OffsetForCodepoint(position:CodepointOffset):Utf8Offset {
		var value:Int = position;
		checkCodepointOffset(value);
		return codepointToUtf8[value];
	}

	public function codepointOffsetForUtf8(offset:Utf8Offset):CodepointOffset {
		var value:Int = offset;
		var low = 0;
		var high = codepointToUtf8.length - 1;
		while (low <= high) {
			var middle = (low + high) >> 1;
			var candidate = codepointToUtf8[middle];
			if (candidate == value)
				return middle;
			if (candidate < value)
				low = middle + 1;
			else
				high = middle - 1;
		}
		throw "UTF-8 offset is not on a code-point boundary";
	}

	public function utf16OffsetForCodepoint(position:CodepointOffset):Utf16Offset {
		var value:Int = position;
		checkCodepointOffset(value);
		return codepointToUtf16[value];
	}

	public function codepointOffsetForUtf16(offset:Utf16Offset):CodepointOffset {
		var value:Int = offset;
		var low = 0;
		var high = codepointToUtf16.length - 1;
		while (low <= high) {
			var middle = (low + high) >> 1;
			var candidate = codepointToUtf16[middle];
			if (candidate == value)
				return middle;
			if (candidate < value)
				low = middle + 1;
			else
				high = middle - 1;
		}
		throw "UTF-16 offset is not on a code-point boundary";
	}

	/** Returns the nearest boundary, preferring the preceding one on a tie. */
	public function graphemePositionAt(position:CodepointOffset):GraphemePosition {
		var value:Int = position;
		checkCodepointOffset(value);
		var previous = previousBoundary(value);
		var next = nextBoundary(value);
		if (next - value < value - previous)
			return next;
		return previous;
	}

	public function codepointOffsetForGrapheme(position:GraphemePosition):CodepointOffset {
		var value:Int = position;
		checkCodepointOffset(value);
		if (!isBoundary(value))
			throw "Grapheme position is not a boundary";
		return value;
	}

	public function isGraphemeBoundary(position:CodepointOffset):Bool {
		var value:Int = position;
		checkCodepointOffset(value);
		return isBoundary(value);
	}

	/** Returns the preceding boundary, or the document start. */
	public function previousGraphemeBoundary(position:CodepointOffset):GraphemePosition {
		var value:Int = position;
		checkCodepointOffset(value);
		return previousBoundary(value);
	}

	/** Returns the following boundary, or the document end. */
	public function nextGraphemeBoundary(position:CodepointOffset):GraphemePosition {
		var value:Int = position;
		checkCodepointOffset(value);
		return nextBoundary(value);
	}

	/**
	 * Returns global grapheme boundaries within a code-point range as
	 * subrange-local offsets. Both range edges are included so paragraph
	 * views remain valid when a grapheme crosses a paragraph boundary.
	 */
	public function graphemeBoundariesForRange(start:CodepointOffset,
			end:CodepointOffset):Array<Int> {
		var first:Int = start;
		var last:Int = end;
		checkRange(first, last);
		var result:Array<Int> = [0];
		for (boundary in graphemeBoundaries)
			if (boundary > first && boundary < last)
				result.push(boundary - first);
		if (last > first)
			result.push(last - first);
		return result;
	}

	public function sliceCodepoints(start:CodepointOffset, end:CodepointOffset):String {
		var first:Int = start;
		var last:Int = end;
		checkRange(first, last);
		var byteStart = codepointToUtf8[first];
		var byteEnd = codepointToUtf8[last];
		return utf8Bytes.sub(byteStart, byteEnd - byteStart).toString();
	}

	public function replaceCodepoints(start:CodepointOffset, end:CodepointOffset,
			replacement:Null<String>):String {
		var first:Int = start;
		var last:Int = end;
		checkRange(first, last);
		var source = Bytes.ofString(text);
		var insert = Bytes.ofString(replacement == null ? "" : replacement);
		var byteStart = codepointToUtf8[first];
		var byteEnd = codepointToUtf8[last];
		var output = Bytes.alloc(byteStart + insert.length + source.length - byteEnd);
		var cursor = 0;
		for (byteIndex in 0...byteStart)
			output.set(cursor++, source.get(byteIndex));
		for (byteIndex in 0...insert.length)
			output.set(cursor++, insert.get(byteIndex));
		for (byteIndex in byteEnd...source.length)
			output.set(cursor++, source.get(byteIndex));
		return output.toString();
	}

	/**
	 * Applies a code-point replacement while retaining unaffected conversion
	 * tables. Only the edited paragraph/grapheme neighborhood is rescanned;
	 * suffix offsets are shifted in place.
	 */
	public function replaceCodepointsIncremental(start:CodepointOffset, end:CodepointOffset,
			replacement:Null<String>, ?nextValue:String):String {
		var first:Int = start;
		var last:Int = end;
		checkRange(first, last);
		var inserted = new TextOffsetMap(replacement == null ? "" : replacement);
		var next = nextValue == null ? replaceCodepoints(first, last, replacement) : nextValue;
		var oldCount = codepointCount;
		var removedCount = last - first;
		var codepointDelta = inserted.codepointCount - removedCount;
		var byteDelta = inserted.utf8ByteLength -
			(codepointToUtf8[last] - codepointToUtf8[first]);
		var utf16Delta = inserted.utf16Length -
			(codepointToUtf16[last] - codepointToUtf16[first]);

		var oldGraphemeStart = previousBoundary(first);
		if (oldGraphemeStart > 0)
			oldGraphemeStart = previousBoundary(oldGraphemeStart - 1);
		var oldGraphemeEnd = nextBoundary(last);
		if (oldGraphemeEnd < oldCount)
			oldGraphemeEnd = nextBoundary(oldGraphemeEnd);
		while (oldGraphemeStart > 0 && isRegionalIndicator(codepoints[oldGraphemeStart - 1]))
			oldGraphemeStart--;
		while (oldGraphemeEnd < oldCount && isRegionalIndicator(codepoints[oldGraphemeEnd]))
			oldGraphemeEnd++;

		var startParagraph = paragraphIndexAt(first);
		if (startParagraph > 0)
			startParagraph--;
		var endParagraph = paragraphIndexAt(last);
		if (endParagraph + 1 < paragraphStarts.length)
			endParagraph++;
		var hasParagraphSuffix = endParagraph + 1 < paragraphStarts.length;
		var oldParagraphStart = paragraphStarts[startParagraph];
		var oldParagraphEnd = paragraphEnds[endParagraph];
		if (oldParagraphEnd < oldCount && codepoints[oldParagraphEnd] == 0x0a)
			oldParagraphEnd++;

		var mutationStart = oldGraphemeStart < oldParagraphStart ? oldGraphemeStart : oldParagraphStart;
		var mutationEnd = oldGraphemeEnd > oldParagraphEnd ? oldGraphemeEnd : oldParagraphEnd;
		var nextCodepoints:Array<Int> = [];
		for (offset in mutationStart...first)
			nextCodepoints.push(codepoints[offset]);
		for (codepoint in inserted.codepoints)
			nextCodepoints.push(codepoint);
		for (offset in last...mutationEnd)
			nextCodepoints.push(codepoints[offset]);

		var baseUtf8 = codepointToUtf8[mutationStart];
		var baseUtf16 = codepointToUtf16[mutationStart];
		var nextUtf8:Array<Int> = [baseUtf8];
		var nextUtf16:Array<Int> = [baseUtf16];
		var utf8Offset = baseUtf8;
		var utf16Offset = baseUtf16;
		for (codepoint in nextCodepoints) {
			utf8Offset += codepointByteLength(codepoint);
			utf16Offset += codepoint > 0xffff ? 2 : 1;
			nextUtf8.push(utf8Offset);
			nextUtf16.push(utf16Offset);
		}

		replaceArrayRange(codepoints, mutationStart, mutationEnd - mutationStart, nextCodepoints);
		replaceArrayRange(codepointToUtf8, mutationStart, mutationEnd - mutationStart + 1, nextUtf8);
		replaceArrayRange(codepointToUtf16, mutationStart, mutationEnd - mutationStart + 1, nextUtf16);
		for (offset in mutationStart + nextCodepoints.length + 1...codepointToUtf8.length)
			codepointToUtf8[offset] += byteDelta;
		for (offset in mutationStart + nextCodepoints.length + 1...codepointToUtf16.length)
			codepointToUtf16[offset] += utf16Delta;

		var newParagraphEnd = oldParagraphEnd + codepointDelta;
		var paragraphStartsReplacement:Array<Int> = [];
		var paragraphEndsReplacement:Array<Int> = [];
		for (offset in oldParagraphStart...newParagraphEnd) {
			var localOffset = offset - mutationStart;
			if (codepoints[localOffset + mutationStart] == 0x0a) {
				paragraphEndsReplacement.push(offset);
				paragraphStartsReplacement.push(offset + 1);
			}
		}
		paragraphEndsReplacement.push(newParagraphEnd);
		if (hasParagraphSuffix && newParagraphEnd > oldParagraphStart &&
			codepoints[newParagraphEnd - 1] == 0x0a) {
			paragraphStartsReplacement.pop();
			paragraphEndsReplacement.pop();
		}
		if (paragraphStartsReplacement.length == 0 ||
			paragraphStartsReplacement[0] != oldParagraphStart)
			paragraphStartsReplacement.unshift(oldParagraphStart);
		replaceArrayRange(paragraphStarts, startParagraph, endParagraph - startParagraph + 1,
			paragraphStartsReplacement);
		replaceArrayRange(paragraphEnds, startParagraph, endParagraph - startParagraph + 1,
			paragraphEndsReplacement);
		var paragraphSuffix = startParagraph + paragraphStartsReplacement.length;
		for (index in paragraphSuffix...paragraphStarts.length) {
			paragraphStarts[index] += codepointDelta;
			paragraphEnds[index] += codepointDelta;
		}

		var newGraphemeEnd = oldGraphemeEnd + codepointDelta;
		var newGraphemeBoundaries:Array<Int> = [oldGraphemeStart];
		var previous = oldGraphemeStart > 0 ? codepoints[oldGraphemeStart - 1] : -1;
		var regionalRun = 0;
		var regionalOffset = oldGraphemeStart - 1;
		while (regionalOffset >= 0 && isRegionalIndicator(codepoints[regionalOffset])) {
			regionalRun++;
			regionalOffset--;
		}
		for (offset in oldGraphemeStart...newGraphemeEnd) {
			var current = codepoints[offset];
			if (offset > oldGraphemeStart && graphemeBreak(previous, current, regionalRun))
				newGraphemeBoundaries.push(offset);
			if (isRegionalIndicator(current))
				regionalRun++;
			else
				regionalRun = 0;
			previous = current;
		}
		if (newGraphemeBoundaries[newGraphemeBoundaries.length - 1] != newGraphemeEnd)
			newGraphemeBoundaries.push(newGraphemeEnd);
		var oldGraphemeBoundaryStart = boundaryIndex(oldGraphemeStart);
		var oldGraphemeBoundaryEnd = boundaryIndex(oldGraphemeEnd);
		replaceArrayRange(graphemeBoundaries, oldGraphemeBoundaryStart,
			oldGraphemeBoundaryEnd - oldGraphemeBoundaryStart + 1, newGraphemeBoundaries);
		var graphemeSuffix = oldGraphemeBoundaryStart + newGraphemeBoundaries.length;
		for (index in graphemeSuffix...graphemeBoundaries.length)
			graphemeBoundaries[index] += codepointDelta;

		text = next;
		utf8Bytes = Bytes.ofString(next);
		codepointCount = oldCount + codepointDelta;
		utf8ByteLength += byteDelta;
		utf16Length += utf16Delta;
		return next;
	}

	/** Returns the paragraph containing a code-point position. */
	public function paragraphRangeAt(position:CodepointOffset):TextRange {
		var value:Int = position;
		checkCodepointOffset(value);
		var paragraphNumber = paragraphIndexAt(value);
		var start = paragraphStarts[paragraphNumber];
		var end = paragraphEnds[paragraphNumber];
		return new TextRange(start, end);
	}

	/** Returns the number of paragraph records, including empty paragraphs. */
	public function paragraphCount():Int
		return paragraphStarts.length;

	/** Returns the paragraph number containing a code-point offset. */
	public function paragraphIndexAtOffset(position:CodepointOffset):Int
		return paragraphIndexAt(position);

	/** Returns a paragraph range by its stable document-order number. */
	public function paragraphRangeAtIndex(index:Int):TextRange {
		if (index < 0 || index >= paragraphStarts.length)
			throw "Paragraph index is outside the string";
		return new TextRange(paragraphStarts[index], paragraphEnds[index]);
	}

	/**
	 * Replaces generated grapheme boundaries with shaping-engine boundaries.
	 * Values are code-point offsets and must be sorted, unique, and inclusive of
	 * both zero and codepointCount.
	 */
	public function setGraphemeBoundaries(boundaries:Array<Int>):Void {
		if (boundaries == null || boundaries.length == 0 || boundaries[0] != 0 ||
			boundaries[boundaries.length - 1] != codepointCount)
			throw "Grapheme boundaries must include the string bounds";
		var copy:Array<Int> = [];
		var previous = -1;
		for (boundary in boundaries) {
			if (boundary < 0 || boundary > codepointCount || boundary <= previous)
				throw "Grapheme boundaries must be sorted and unique";
			copy.push(boundary);
			previous = boundary;
		}
		graphemeBoundaries = copy;
	}

	/** Replaces authoritative shaping boundaries for one document subrange. */
	public function replaceGraphemeBoundariesForRange(start:CodepointOffset,
			end:CodepointOffset, boundaries:Array<Int>):Void {
		var first:Int = start;
		var last:Int = end;
		checkRange(first, last);
		if (boundaries == null || boundaries.length == 0 || boundaries[0] != first ||
			boundaries[boundaries.length - 1] != last)
			throw "Grapheme range boundaries must include the range bounds";
		var previous = first - 1;
		for (boundary in boundaries) {
			if (boundary < first || boundary > last || boundary <= previous)
				throw "Grapheme range boundaries must be sorted and unique";
			previous = boundary;
		}
		var boundaryStart = boundaryIndex(first);
		var boundaryEnd = boundaryIndex(last);
		replaceArrayRange(graphemeBoundaries, boundaryStart,
			boundaryEnd - boundaryStart + 1, boundaries);
	}

	public function graphemeBoundaryCount():Int
		return graphemeBoundaries.length;

	public function graphemeCount():Int
		return graphemeBoundaries.length - 1;

	public function graphemeBoundaryAt(boundaryNumber:Int):GraphemePosition {
		if (boundaryNumber < 0 || boundaryNumber >= graphemeBoundaries.length)
			throw "Grapheme boundary number is outside the string";
		return graphemeBoundaries[boundaryNumber];
	}

	function checkCodepointOffset(value:Int):Void {
		if (value < 0 || value > codepointCount)
			throw "Code-point offset is outside the string";
	}

	function checkRange(start:Int, end:Int):Void {
		if (start < 0 || end < start || end > codepointCount)
			throw "Code-point range is outside the string";
	}

	function paragraphIndexAt(value:Int):Int {
		var low = 0;
		var high = paragraphStarts.length - 1;
		var paragraphNumber = 0;
		while (low <= high) {
			var middle = (low + high) >> 1;
			if (paragraphStarts[middle] <= value) {
				paragraphNumber = middle;
				low = middle + 1;
			} else {
				high = middle - 1;
			}
		}
		return paragraphNumber;
	}

	function boundaryIndex(value:Int):Int {
		var low = 0;
		var high = graphemeBoundaries.length - 1;
		while (low <= high) {
			var middle = (low + high) >> 1;
			var candidate = graphemeBoundaries[middle];
			if (candidate == value)
				return middle;
			if (candidate < value)
				low = middle + 1;
			else
				high = middle - 1;
		}
		throw "Grapheme boundary is missing";
	}

	static function replaceArrayRange<T>(array:Array<T>, start:Int, count:Int,
			replacement:Array<T>):Void {
		var oldLength = array.length;
		var delta = replacement.length - count;
		var suffixStart = start + count;
		if (delta > 0) {
			array.resize(oldLength + delta);
			var index = oldLength - 1;
			while (index >= suffixStart) {
				array[index + delta] = array[index];
				index--;
			}
		} else if (delta < 0) {
			var index = suffixStart;
			while (index < oldLength) {
				array[index + delta] = array[index];
				index++;
			}
			array.resize(oldLength + delta);
		}
		for (index in 0...replacement.length)
			array[start + index] = replacement[index];
	}

	static function codepointByteLength(value:Int):Int
		return value <= 0x7f ? 1 : value <= 0x7ff ? 2 : value <= 0xffff ? 3 : 4;

	function isBoundary(value:Int):Bool {
		var low = 0;
		var high = graphemeBoundaries.length - 1;
		while (low <= high) {
			var middle = (low + high) >> 1;
			var candidate = graphemeBoundaries[middle];
			if (candidate == value)
				return true;
			if (candidate < value)
				low = middle + 1;
			else
				high = middle - 1;
		}
		return false;
	}

	function previousBoundary(value:Int):Int {
		var low = 0;
		var high = graphemeBoundaries.length - 1;
		var result = 0;
		while (low <= high) {
			var middle = (low + high) >> 1;
			if (graphemeBoundaries[middle] <= value) {
				result = graphemeBoundaries[middle];
				low = middle + 1;
			} else {
				high = middle - 1;
			}
		}
		return result;
	}

	function nextBoundary(value:Int):Int {
		var low = 0;
		var high = graphemeBoundaries.length - 1;
		var result = codepointCount;
		while (low <= high) {
			var middle = (low + high) >> 1;
			if (graphemeBoundaries[middle] > value) {
				result = graphemeBoundaries[middle];
				high = middle - 1;
			} else {
				low = middle + 1;
			}
		}
		return result;
	}

	function buildGraphemeBoundaries():Array<Int> {
		var result:Array<Int> = [0];
		var previous = -1;
		var regionalRun = 0;
		for (currentOffset in 0...codepoints.length) {
			var current = codepoints[currentOffset];
			var shouldBreak = currentOffset > 0 && graphemeBreak(previous, current, regionalRun);
			if (shouldBreak)
				result.push(currentOffset);
			if (isRegionalIndicator(current))
				regionalRun++;
			else
				regionalRun = 0;
			previous = current;
		}
		result.push(codepoints.length);
		return result;
	}

	function graphemeBreak(previous:Int, current:Int, regionalRun:Int):Bool {
		if (previous == 0x0d && current == 0x0a)
			return false;
		if (isControl(previous) || isControl(current))
			return true;
		if (isExtend(current) || isSpacingMark(current) || current == 0x200d)
			return false;
		if (previous == 0x200d || isIndicLinker(previous))
			return false;
		if (isRegionalIndicator(previous) && isRegionalIndicator(current))
			return regionalRun % 2 == 0;
		if (isHangulNoBreak(previous, current))
			return false;
		return true;
	}

	static function isControl(value:Int):Bool
		return value == 0x0d || value == 0x0a || value == 0x00 ||
			(value >= 0x01 && value <= 0x1f) || (value >= 0x7f && value <= 0x9f);

	static function isExtend(value:Int):Bool
		return (value >= 0x0300 && value <= 0x036f) ||
			(value >= 0x0483 && value <= 0x0489) ||
			(value >= 0x0591 && value <= 0x05bd) ||
			(value >= 0x05bf && value <= 0x05c7) ||
			(value >= 0x0610 && value <= 0x061a) ||
			(value >= 0x064b && value <= 0x065f) ||
			(value >= 0x0670 && value <= 0x0670) ||
			(value >= 0x06d6 && value <= 0x06ed) ||
			(value >= 0x0711 && value <= 0x0711) ||
			(value >= 0x0730 && value <= 0x074a) ||
			(value >= 0x07a6 && value <= 0x07b0) ||
			(value >= 0x07eb && value <= 0x07f3) ||
			(value >= 0x0816 && value <= 0x0819) ||
			(value >= 0x081b && value <= 0x0823) ||
			(value >= 0x0825 && value <= 0x0827) ||
			(value >= 0x0829 && value <= 0x082d) ||
			(value >= 0x0859 && value <= 0x085b) ||
			(value >= 0x08d3 && value <= 0x0902) ||
			(value >= 0x093a && value <= 0x094f) ||
			(value >= 0x0951 && value <= 0x0957) ||
			(value >= 0x0962 && value <= 0x0963) ||
			(value >= 0x0981 && value <= 0x0981) ||
			(value >= 0x09bc && value <= 0x09cd) ||
			(value >= 0x09e2 && value <= 0x09e3) ||
			(value >= 0x0a01 && value <= 0x0a4d) ||
			(value >= 0x0a70 && value <= 0x0a71) ||
			(value >= 0x0a81 && value <= 0x0acd) ||
			(value >= 0x0ae2 && value <= 0x0ae3) ||
			(value >= 0x0afa && value <= 0x0b4d) ||
			(value >= 0x0b56 && value <= 0x0bcd) ||
			(value >= 0x0c00 && value <= 0x0c4d) ||
			(value >= 0x0c55 && value <= 0x0ccd) ||
			(value >= 0x0cd5 && value <= 0x0cdc) ||
			(value >= 0x0cf3 && value <= 0x0cf3) ||
			(value >= 0x0d00 && value <= 0x0d4d) ||
			(value >= 0x0d57 && value <= 0x0d57) ||
			(value >= 0x0d62 && value <= 0x0d63) ||
			(value >= 0x0d81 && value <= 0x0dca) ||
			(value >= 0x0dd2 && value <= 0x0ddf) ||
			(value >= 0x0e31 && value <= 0x0e4e) ||
			(value >= 0x0eb1 && value <= 0x0ecd) ||
			(value >= 0x0f18 && value <= 0x0fbc) ||
			(value >= 0x102b && value <= 0x103e) ||
			(value >= 0x1056 && value <= 0x1059) ||
			(value >= 0x105e && value <= 0x105f) ||
			(value >= 0x1062 && value <= 0x1064) ||
			(value >= 0x1067 && value <= 0x106d) ||
			(value >= 0x1071 && value <= 0x1074) ||
			(value >= 0x1082 && value <= 0x108d) ||
			(value >= 0x108f && value <= 0x109d) ||
			(value >= 0x135d && value <= 0x135f) ||
			(value >= 0x1712 && value <= 0x1714) ||
			(value >= 0x1732 && value <= 0x1734) ||
			(value >= 0x1752 && value <= 0x1753) ||
			(value >= 0x1772 && value <= 0x1773) ||
			(value >= 0x17b4 && value <= 0x17d3) ||
			(value >= 0x180b && value <= 0x180f) ||
			(value >= 0x1885 && value <= 0x1886) ||
			(value >= 0x18a9 && value <= 0x18a9) ||
			(value >= 0x1920 && value <= 0x193b) ||
			(value >= 0x1a17 && value <= 0x1a1b) ||
			(value >= 0x1a55 && value <= 0x1a7f) ||
			(value >= 0x1ab0 && value <= 0x1aff) ||
			(value >= 0x1b00 && value <= 0x1b04) ||
			(value >= 0x1b34 && value <= 0x1b44) ||
			(value >= 0x1b6b && value <= 0x1b73) ||
			(value >= 0x1b80 && value <= 0x1b82) ||
			(value >= 0x1ba1 && value <= 0x1bad) ||
			(value >= 0x1be6 && value <= 0x1bf3) ||
			(value >= 0x1c24 && value <= 0x1c37) ||
			(value >= 0x1cd0 && value <= 0x1cff) ||
			(value >= 0x1dc0 && value <= 0x1dff) ||
			(value >= 0x20d0 && value <= 0x20ff) ||
			(value >= 0x2cef && value <= 0x2cf1) ||
			(value >= 0x2de0 && value <= 0x2dff) ||
			(value >= 0xa66f && value <= 0xa672) ||
			(value >= 0xa674 && value <= 0xa67d) ||
			(value >= 0xa69e && value <= 0xa69f) ||
			(value >= 0xa6f0 && value <= 0xa6f1) ||
			(value >= 0xa802 && value <= 0xa802) ||
			(value >= 0xa806 && value <= 0xa806) ||
			(value >= 0xa80b && value <= 0xa80b) ||
			(value >= 0xa823 && value <= 0xa827) ||
			(value >= 0xa880 && value <= 0xa881) ||
			(value >= 0xa8b4 && value <= 0xa8c5) ||
			(value >= 0xa8e2 && value <= 0xa8f1) ||
			(value >= 0xa926 && value <= 0xa92f) ||
			(value >= 0xa947 && value <= 0xa953) ||
			(value >= 0xa980 && value <= 0xa983) ||
			(value >= 0xa9b3 && value <= 0xa9c0) ||
			(value >= 0xa9e5 && value <= 0xa9e5) ||
			(value >= 0xaa29 && value <= 0xaa40) ||
			(value >= 0xaa43 && value <= 0xaa43) ||
			(value >= 0xaa4c && value <= 0xaa4d) ||
			(value >= 0xaa7b && value <= 0xaa7d) ||
			(value >= 0xaab0 && value <= 0xaab1) ||
			(value >= 0xaab2 && value <= 0xaab4) ||
			(value >= 0xaab7 && value <= 0xaab8) ||
			(value >= 0xaabe && value <= 0xaabf) ||
			(value >= 0xaac1 && value <= 0xaac1) ||
			(value >= 0xaaec && value <= 0xaaed) ||
			(value >= 0xaaf3 && value <= 0xaaf5) ||
			(value >= 0xabe3 && value <= 0xabe4) ||
			(value >= 0xabe6 && value <= 0xabe7) ||
			(value >= 0xabe9 && value <= 0xab0c) ||
			(value >= 0xfb1e && value <= 0xfb1e) ||
			(value >= 0xfe00 && value <= 0xfe0f) ||
			(value >= 0xfe20 && value <= 0xfe2f) ||
			(value >= 0xff9e && value <= 0xff9f) ||
			(value >= 0x101fd && value <= 0x101fd) ||
			(value >= 0x102e0 && value <= 0x102e0) ||
			(value >= 0x10376 && value <= 0x1037a) ||
			(value >= 0x10a01 && value <= 0x10a0f) ||
			(value >= 0x10a38 && value <= 0x10a3f) ||
			(value >= 0x10ae5 && value <= 0x10ae6) ||
			(value >= 0x11000 && value <= 0x11002) ||
			(value >= 0x11038 && value <= 0x11046) ||
			(value >= 0x11070 && value <= 0x11082) ||
			(value >= 0x110b0 && value <= 0x110ba) ||
			(value >= 0x11100 && value <= 0x11102) ||
			(value >= 0x11127 && value <= 0x11144) ||
			(value >= 0x11173 && value <= 0x11175) ||
			(value >= 0x11180 && value <= 0x11182) ||
			(value >= 0x111b0 && value <= 0x111c0) ||
			(value >= 0x111c9 && value <= 0x111cc) ||
			(value >= 0x111cf && value <= 0x111cf) ||
			(value >= 0x1122c && value <= 0x11237) ||
			(value >= 0x1123e && value <= 0x11241) ||
			(value >= 0x112df && value <= 0x112ea) ||
			(value >= 0x11300 && value <= 0x11304) ||
			(value >= 0x1133b && value <= 0x1133c) ||
			(value >= 0x11340 && value <= 0x1134d) ||
			(value >= 0x11366 && value <= 0x1136c) ||
			(value >= 0x11370 && value <= 0x11374) ||
			(value >= 0x11438 && value <= 0x11446) ||
			(value >= 0x114b0 && value <= 0x114c3) ||
			(value >= 0x114c6 && value <= 0x114c6) ||
			(value >= 0x115af && value <= 0x115c0) ||
			(value >= 0x115dc && value <= 0x115dd) ||
			(value >= 0x11630 && value <= 0x11640) ||
			(value >= 0x116ab && value <= 0x116b7) ||
			(value >= 0x1171d && value <= 0x1172b) ||
			(value >= 0x11740 && value <= 0x11746) ||
			(value >= 0x1182c && value <= 0x1183a) ||
			(value >= 0x11930 && value <= 0x1193a) ||
			(value >= 0x1193e && value <= 0x1193f) ||
			(value >= 0x11941 && value <= 0x11943) ||
			(value >= 0x119d1 && value <= 0x119e0) ||
			(value >= 0x11a01 && value <= 0x11a0a) ||
			(value >= 0x11a33 && value <= 0x11a39) ||
			(value >= 0x11a3b && value <= 0x11a3e) ||
			(value >= 0x11a47 && value <= 0x11a4f) ||
			(value >= 0x11a51 && value <= 0x11a56) ||
			(value >= 0x11a59 && value <= 0x11a5b) ||
			(value >= 0x11a8a && value <= 0x11a99) ||
			(value >= 0x11c30 && value <= 0x11c3f) ||
			(value >= 0x11c92 && value <= 0x11ca7) ||
			(value >= 0x11ca9 && value <= 0x11cb6) ||
			(value >= 0x11d31 && value <= 0x11d45) ||
			(value >= 0x11d47 && value <= 0x11d47) ||
			(value >= 0x11d90 && value <= 0x11d97) ||
			(value >= 0x11ef3 && value <= 0x11ef6) ||
			(value >= 0x16af0 && value <= 0x16af4) ||
			(value >= 0x16b30 && value <= 0x16b36) ||
			(value >= 0x16f4f && value <= 0x16f4f) ||
			(value >= 0x16f8f && value <= 0x16f92) ||
			(value >= 0x1bc9d && value <= 0x1bc9e) ||
			(value >= 0x1d167 && value <= 0x1d169) ||
			(value >= 0x1d17b && value <= 0x1d182) ||
			(value >= 0x1d185 && value <= 0x1d18b) ||
			(value >= 0x1d1aa && value <= 0x1d1ad) ||
			(value >= 0x1d242 && value <= 0x1d244) ||
			(value >= 0x1da00 && value <= 0x1da36) ||
			(value >= 0x1da3b && value <= 0x1da6c) ||
			(value >= 0x1da75 && value <= 0x1da75) ||
			(value >= 0x1da84 && value <= 0x1da84) ||
			(value >= 0x1da9b && value <= 0x1daa0) ||
			(value >= 0x1daa2 && value <= 0x1dab0) ||
			(value >= 0x1e000 && value <= 0x1e02a) ||
			(value >= 0x1e130 && value <= 0x1e136) ||
			(value >= 0x1e2ae && value <= 0x1e2ae) ||
			(value >= 0x1e2ec && value <= 0x1e2ef) ||
			(value >= 0x1e4ec && value <= 0x1e4ef) ||
			(value >= 0x1e8d0 && value <= 0x1e8d6) ||
			(value >= 0x1e944 && value <= 0x1e94a) ||
			(value >= 0xe0100 && value <= 0xe01ef) ||
			(value >= 0x1f3fb && value <= 0x1f3ff);

	static function isSpacingMark(value:Int):Bool
		return value == 0x0903 || value == 0x093b ||
			(value >= 0x093e && value <= 0x0940) ||
			(value >= 0x0949 && value <= 0x094c) ||
			(value >= 0x0982 && value <= 0x0983) ||
			(value >= 0x09be && value <= 0x09c0) ||
			(value >= 0x09c7 && value <= 0x09c8) ||
			(value >= 0x09cb && value <= 0x09cc) ||
			(value >= 0x0a03 && value <= 0x0a03) ||
			(value >= 0x0abe && value <= 0x0ac0) ||
			(value >= 0x0ac9 && value <= 0x0acc) ||
			(value >= 0x0b02 && value <= 0x0b03) ||
			(value >= 0x0b3e && value <= 0x0b40) ||
			(value >= 0x0b47 && value <= 0x0b48) ||
			(value >= 0x0b4b && value <= 0x0b4c) ||
			(value >= 0x0c01 && value <= 0x0c04) ||
			(value >= 0x0c41 && value <= 0x0c44) ||
			(value >= 0x0c82 && value <= 0x0c83) ||
			(value >= 0x0cbe && value <= 0x0cc4) ||
			(value >= 0x0cc7 && value <= 0x0cc8) ||
			(value >= 0x0cca && value <= 0x0ccb) ||
			(value >= 0x0d02 && value <= 0x0d03) ||
			(value >= 0x0d3e && value <= 0x0d40) ||
			(value >= 0x0d46 && value <= 0x0d48) ||
			(value >= 0x0d4a && value <= 0x0d4c) ||
			(value >= 0x0d82 && value <= 0x0d83) ||
			(value >= 0x0dd0 && value <= 0x0dd1) ||
			(value >= 0x0dd8 && value <= 0x0dde) ||
			(value >= 0x0f3e && value <= 0x0f3f) ||
			(value >= 0x102b && value <= 0x1030) ||
			(value >= 0x1062 && value <= 0x1064) ||
			(value >= 0x1067 && value <= 0x106d) ||
			(value >= 0x1083 && value <= 0x1086) ||
			(value >= 0x17b6 && value <= 0x17c8) ||
			(value >= 0x1a55 && value <= 0x1a5e) ||
			(value >= 0x1b35 && value <= 0x1b44) ||
			(value >= 0x1b6b && value <= 0x1b73) ||
			(value >= 0x1dc0 && value <= 0x1dff) ||
			(value >= 0xa823 && value <= 0xa827) ||
			(value >= 0xa880 && value <= 0xa881) ||
			(value >= 0xa8b4 && value <= 0xa8c5) ||
			(value >= 0xa952 && value <= 0xa953) ||
			(value >= 0xa983 && value <= 0xa983) ||
			(value >= 0xa9b4 && value <= 0xa9c0) ||
			(value >= 0xaa2f && value <= 0xaa30) ||
			(value >= 0xaa33 && value <= 0xaa34) ||
			(value >= 0xaa4d && value <= 0xaa4d) ||
			(value >= 0xaa7b && value <= 0xaa7d) ||
			(value >= 0xabe3 && value <= 0xabe4) ||
			(value >= 0xabe6 && value <= 0xabe7) ||
			(value >= 0xabe9 && value <= 0xab0c);

	static function isRegionalIndicator(value:Int):Bool
		return value >= 0x1f1e6 && value <= 0x1f1ff;

	static function isIndicLinker(value:Int):Bool
		return value == 0x094d || value == 0x09cd || value == 0x0a4d ||
			value == 0x0acd || value == 0x0b4d || value == 0x0bcd ||
			value == 0x0c4d || value == 0x0ccd || value == 0x0d4d ||
			value == 0x0dca || value == 0x1039 || value == 0x103a ||
			value == 0x1714 || value == 0x1734 || value == 0x17d2 ||
			value == 0x1a60 || value == 0xa806 || value == 0xa8c4 ||
			value == 0xa953 || value == 0xa9c0 || value == 0xaa4d ||
			value == 0xaaf6 || value == 0xabed;

	static function isHangulNoBreak(previous:Int, current:Int):Bool {
		var previousType = hangulType(previous);
		var currentType = hangulType(current);
		// UAX #29 GB6-GB8: L × (L|V|LV|LVT),
		// V|LV × (V|T), and T|LVT × T. `hangulType()` uses
		// 3 for both T and LVT, and 4 for LV.
		return previousType == 1 &&
			(currentType == 1 || currentType == 2 || currentType == 3 || currentType == 4) ||
			(previousType == 2 || previousType == 4) && (currentType == 2 || currentType == 3) ||
			previousType == 3 && currentType == 3;
	}

	static function hangulType(value:Int):Int {
		if (value >= 0x1100 && value <= 0x115f || value >= 0xa960 && value <= 0xa97c)
			return 1;
		if (value >= 0x1160 && value <= 0x11a7 || value >= 0xd7b0 && value <= 0xd7c6)
			return 2;
		if (value >= 0x11a8 && value <= 0x11ff || value >= 0xd7cb && value <= 0xd7fb)
			return 3;
		if (value >= 0xac00 && value <= 0xd7a3)
			return (value - 0xac00) % 28 == 0 ? 4 : 3;
		return 0;
	}

	static function decodeCodepoint(bytes:Bytes, offset:Int):Int {
		var first = bytes.get(offset);
		if (first < 0x80)
			return first;
		var count = utf8Width(first);
		if (count == 0 || offset > bytes.length - count)
			throw "Invalid UTF-8 text";
		var value = first & (count == 2 ? 0x1f : count == 3 ? 0x0f : 0x07);
		for (byteIndex in 1...count) {
			var next = bytes.get(offset + byteIndex);
			if (next < 0x80 || next > 0xbf)
				throw "Invalid UTF-8 text";
			value = (value << 6) | (next & 0x3f);
		}
		if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
			(count == 4 && value < 0x10000) || value > 0x10ffff ||
			(value >= 0xd800 && value <= 0xdfff))
			throw "Invalid UTF-8 text";
		return value;
	}

	static function nextOffset(bytes:Bytes, offset:Int):Int {
		var first = bytes.get(offset);
		var count = utf8Width(first);
		if (count == 0 || offset > bytes.length - count)
			throw "Invalid UTF-8 text";
		for (byteIndex in 1...count) {
			var next = bytes.get(offset + byteIndex);
			if (next < 0x80 || next > 0xbf)
				throw "Invalid UTF-8 text";
		}
		if (count >= 3) {
			var second = bytes.get(offset + 1);
			if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
				(first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
				throw "Invalid UTF-8 text";
		}
		return offset + count;
	}

	static function utf8Width(first:Int):Int
		return first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
			first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
}
