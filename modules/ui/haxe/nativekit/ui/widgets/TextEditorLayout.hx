package nativekit.ui.widgets;

import FontCollection;
import Canvas;
import Color;
import LayoutMeasureConstraints;
import LayoutMeasureResult;
import ParagraphStyle;
import Rect;
import TextLayout;
import TextStyle;

/** Retained paragraph layouts backing one editor document. */
class TextEditorLayout {
	public var text(default, null):String;
	public var width(default, null):Float;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	public var paragraphCount(get, never):Int;

	final fonts:FontCollection;
	var paragraphs:Array<TextEditorParagraphRecord>;
	var offsets:TextOffsetMap;
	var contentWidth:Float;
	var contentHeight:Float;
	var disposed:Bool;

	public function new(fonts:FontCollection, value:String, width:Float, textStyle:TextStyle,
			paragraphStyle:ParagraphStyle, ?offsetMap:TextOffsetMap) {
		if (fonts == null || fonts.isDisposed())
			throw "Editor layout requires a live font collection";
		if (width <= 0.0 || textStyle == null || paragraphStyle == null)
			throw "Editor layout arguments are invalid";
		this.fonts = fonts;
		this.textStyle = copyTextStyle(textStyle);
		this.paragraphStyle = copyParagraphStyle(paragraphStyle);
		paragraphs = [];
		offsets = null;
		contentWidth = 0.0;
		contentHeight = 0.0;
		disposed = false;
		update(value, width, this.textStyle, this.paragraphStyle, offsetMap);
	}

	function get_paragraphCount():Int
		return paragraphs.length;

	/** Updates only paragraph resources whose text or shaping inputs changed. */
	public function update(value:String, nextWidth:Float, nextTextStyle:TextStyle,
			nextParagraphStyle:ParagraphStyle, ?offsetMap:TextOffsetMap):Void {
		ensureLive();
		if (nextWidth <= 0.0 || nextTextStyle == null || nextParagraphStyle == null)
			throw "Editor layout update arguments are invalid";
		var actualText = value == null ? "" : value;
		var nextOffsets = offsetMap == null ? new TextOffsetMap(actualText) : offsetMap;
		if (nextOffsets.text != actualText)
			nextOffsets = new TextOffsetMap(actualText);
		var styleChanged = textStyle.font != nextTextStyle.font ||
			textStyle.fontSize != nextTextStyle.fontSize ||
			textStyle.letterSpacing != nextTextStyle.letterSpacing ||
			paragraphStyle.wrap != nextParagraphStyle.wrap ||
			paragraphStyle.alignment != nextParagraphStyle.alignment ||
			paragraphStyle.lineHeight != nextParagraphStyle.lineHeight ||
			paragraphStyle.direction != nextParagraphStyle.direction;
		textStyle.font = nextTextStyle.font;
		textStyle.fontSize = nextTextStyle.fontSize;
		textStyle.letterSpacing = nextTextStyle.letterSpacing;
		paragraphStyle.wrap = nextParagraphStyle.wrap;
		paragraphStyle.alignment = nextParagraphStyle.alignment;
		paragraphStyle.lineHeight = nextParagraphStyle.lineHeight;
		paragraphStyle.direction = nextParagraphStyle.direction;

		var previous = paragraphs;
		var reusable = new Map<String, Array<TextEditorParagraphRecord>>();
		for (record in previous) {
			var records = reusable.get(record.text);
			if (records == null) {
				records = [];
				reusable.set(record.text, records);
			}
			records.push(record);
		}
		var used:Array<TextEditorParagraphRecord> = [];
		var next:Array<TextEditorParagraphRecord> = [];
		var nextCount = nextOffsets.paragraphCount();
		for (index in 0...nextCount) {
			var range = nextOffsets.paragraphRangeAtIndex(index);
			var paragraphText = nextOffsets.sliceCodepoints(range.start, range.end);
			var previousRecord = index < previous.length ? previous[index] : null;
			var record:TextEditorParagraphRecord = null;
			if (previousRecord != null && !containsRecord(used, previousRecord) &&
				previousRecord.text == paragraphText &&
				previousRecord.layout.width == nextWidth && !styleChanged) {
				record = previousRecord;
			} else if (!styleChanged) {
				var matching = reusable.get(paragraphText);
				if (matching != null)
					while (matching.length > 0 && record == null) {
						var candidate:TextEditorParagraphRecord = matching.pop();
						if (candidate != null && candidate.layout.width == nextWidth &&
							!containsRecord(used, candidate))
							record = candidate;
					}
			}
			if (record == null && previousRecord != null && !containsRecord(used, previousRecord))
				record = previousRecord;
			if (record != null) {
				var textChanged = record.text != paragraphText;
				if (textChanged || record.layout.width != nextWidth || styleChanged) {
					record.layout.update(paragraphText, nextWidth, textStyle, paragraphStyle);
					record.text = paragraphText;
					if (textChanged)
						record.graphemeBoundaries = record.layout.graphemeBoundaries();
				}
			} else {
				record = new TextEditorParagraphRecord(paragraphText,
					TextLayout.create(fonts, paragraphText, nextWidth, textStyle, paragraphStyle));
			}
			used.push(record);
			record.start = range.start;
			record.end = range.end;
			record.y = 0.0;
			record.height = 0.0;
			next.push(record);
		}
		for (record in previous)
			if (!containsRecord(used, record))
				record.layout.dispose();
		syncGraphemeBoundaries(nextOffsets, next);

		text = actualText;
		width = nextWidth;
		offsets = nextOffsets;
		paragraphs = next;
		recomputeMetrics();
	}

	/** Replaces fallback boundaries with boundaries reported by Skribidi. */
	function syncGraphemeBoundaries(nextOffsets:TextOffsetMap,
			next:Array<TextEditorParagraphRecord>):Void {
		var boundaries:Array<Int> = [0];
		for (record in next) {
			var hasFollowingLineFeed = record.end < nextOffsets.codepointCount;
			var endsCrLf = hasFollowingLineFeed && record.text.length > 0 &&
				record.text.charCodeAt(record.text.length - 1) == 0x0d;
			for (index in 1...record.graphemeBoundaries.length) {
				var boundary = record.start + record.graphemeBoundaries[index];
				if (!endsCrLf || boundary != record.end)
					appendBoundary(boundaries, boundary);
			}
			if (hasFollowingLineFeed) {
				if (!endsCrLf)
					appendBoundary(boundaries, record.end);
				appendBoundary(boundaries, record.end + 1);
			} else {
				appendBoundary(boundaries, record.end);
			}
		}
		if (boundaries[boundaries.length - 1] != nextOffsets.codepointCount)
			appendBoundary(boundaries, nextOffsets.codepointCount);
		nextOffsets.setGraphemeBoundaries(boundaries);
	}

	static function appendBoundary(boundaries:Array<Int>, value:Int):Void {
		if (value > boundaries[boundaries.length - 1])
			boundaries.push(value);
	}

	public function setText(value:String, ?offsetMap:TextOffsetMap):Void
		update(value, width, textStyle, paragraphStyle, offsetMap);

	public function measure():TextMetrics
		return new TextMetrics(0.0, 0.0, contentWidth, contentHeight);

	/** Measures this retained content against the constraints of a Custom node. */
	public function measureForConstraints(constraints:LayoutMeasureConstraints):LayoutMeasureResult {
		ensureLive();
		if (constraints == null || constraints.maxWidth < constraints.minWidth ||
			constraints.maxHeight < constraints.minHeight)
			throw "Editor layout constraints are invalid";
		var measured = measure();
		var measuredWidth = Math.max(measured.width, constraints.minWidth);
		if (Math.isFinite(constraints.maxWidth))
			measuredWidth = Math.min(measuredWidth, constraints.maxWidth);
		var measuredHeight = Math.max(measured.height, constraints.minHeight);
		if (Math.isFinite(constraints.maxHeight))
			measuredHeight = Math.min(measuredHeight, constraints.maxHeight);
		var baseline = 0.0;
		var hasBaseline = paragraphs.length > 0 && paragraphs[0].height > 0.0;
		if (hasBaseline) {
			var firstCaret = paragraphs[0].layout.caret(new TextPosition(0, 0));
			baseline = firstCaret.y;
			hasBaseline = Math.isFinite(baseline) && baseline >= 0.0 && baseline <= measuredHeight;
		}
		return new LayoutMeasureResult(measuredWidth, measuredHeight, baseline, hasBaseline);
	}

	/** Paints each retained paragraph in document order at its cached y offset. */
	public function paint(canvas:Canvas, color:Color):Void {
		ensureLive();
		if (canvas == null || color == null)
			throw "Editor layout paint arguments are invalid";
		for (record in paragraphs) {
			record.layout.setColor(color);
			if (record.text.length > 0)
				canvas.drawText(record.layout, 0.0, record.y);
		}
	}

	public function hitTest(x:Float, y:Float):TextPosition {
		ensureLive();
		if (paragraphs.length == 0)
			return new TextPosition(0, 0);
		var record = paragraphAtY(y);
		var hit = record.layout.hitTest(x, y - record.y);
		return new TextPosition(clamp(hit.offset + record.start, record.start, record.end), hit.affinity);
	}

	public function offsetFromPosition(position:TextPosition):Int {
		ensureLive();
		if (position == null)
			throw "Text position cannot be null";
		var record = paragraphAtOffset(position.offset);
		return clamp(record.layout.offsetFromPosition(
			new TextPosition(clamp(position.offset - record.start, 0, record.end - record.start),
				position.affinity)) + record.start, record.start, record.end);
	}

	public function caret(position:TextPosition):TextCaret {
		ensureLive();
		if (position == null)
			throw "Text position cannot be null";
		var record = paragraphAtOffset(position.offset);
		var local = clamp(position.offset - record.start, 0, record.end - record.start);
		var value = record.layout.caret(new TextPosition(local, position.affinity));
		return new TextCaret(value.x, value.y + record.y, value.ascender, value.descender,
			value.slope, value.direction);
	}

	public function selectionRects(start:TextPosition, end:TextPosition):Array<Rect> {
		ensureLive();
		if (start == null || end == null)
			throw "Text selection endpoints cannot be null";
		var first = clamp(start.offset, 0, offsets.codepointCount);
		var last = clamp(end.offset, 0, offsets.codepointCount);
		if (last < first) {
			var swap = first;
			first = last;
			last = swap;
		}
		if (first == last)
			return [];
		var result:Array<Rect> = [];
		for (record in paragraphs) {
			var localStart:Int = first > record.start ? first : record.start;
			var localEnd:Int = last < record.end ? last : record.end;
			if (localEnd <= localStart)
				continue;
			for (rect in record.layout.selectionRects(
				new TextPosition(localStart - record.start, start.affinity),
				new TextPosition(localEnd - record.start, end.affinity)))
				result.push(new Rect(rect.x, rect.y + record.y, rect.width, rect.height));
		}
		return result;
	}

	public function nextGrapheme(offset:Int):Int {
		ensureLive();
		var recordIndex = paragraphIndexAtOffset(offset);
		var record = paragraphs[recordIndex];
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var next = record.layout.nextGrapheme(local);
		if (next == local && local >= record.end - record.start && recordIndex + 1 < paragraphs.length)
			return paragraphs[recordIndex + 1].start;
		return clamp(record.start + next, record.start, record.end);
	}

	public function previousGrapheme(offset:Int):Int {
		ensureLive();
		var recordIndex = paragraphIndexAtOffset(offset);
		var record = paragraphs[recordIndex];
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var previous = record.layout.previousGrapheme(local);
		if (previous == local && local == 0 && recordIndex > 0)
			return paragraphs[recordIndex - 1].end;
		return clamp(record.start + previous, record.start, record.end);
	}

	public function alignGrapheme(offset:Int):Int {
		ensureLive();
		var record = paragraphAtOffset(offset);
		var local = clamp(offset - record.start, 0, record.end - record.start);
		return clamp(record.start + record.layout.alignGrapheme(local), record.start, record.end);
	}

	public function wordRange(position:TextPosition):Array<Int> {
		ensureLive();
		var record = paragraphAtOffset(position.offset);
		var local = clamp(position.offset - record.start, 0, record.end - record.start);
		var range = record.layout.wordRange(new TextPosition(local, position.affinity));
		return [record.start + range[0], record.start + range[1]];
	}

	public function wordRangeAt(offset:Int):TextRange {
		ensureLive();
		var record = paragraphAtOffset(offset);
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var range = record.layout.wordRangeAt(local);
		return new TextRange(record.start + range.start, record.start + range.end);
	}

	public function lineRangeAt(offset:Int):TextRange {
		ensureLive();
		var record = paragraphAtOffset(offset);
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var range = record.layout.lineRangeAt(local);
		var end = record.start + range.end;
		if (end == record.end && end < offsets.codepointCount)
			end++;
		return new TextRange(record.start + range.start, end);
	}

	public function moveWord(offset:Int, direction:Int, macStyle:Bool = false):Int {
		ensureLive();
		if (direction != -1 && direction != 1)
			throw "Text word movement arguments are invalid";
		var recordIndex = paragraphIndexAtOffset(offset);
		var record = paragraphs[recordIndex];
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var moved = record.layout.moveWord(local, direction, macStyle);
		if (moved == local && local >= record.end - record.start && direction > 0 &&
			recordIndex + 1 < paragraphs.length)
			return paragraphs[recordIndex + 1].start;
		if (moved == local && local == 0 && direction < 0 && recordIndex > 0)
			return paragraphs[recordIndex - 1].end;
		return clamp(record.start + moved, record.start, record.end);
	}

	public function moveParagraph(offset:Int, direction:Int, macStyle:Bool = false):Int {
		ensureLive();
		if (direction != -1 && direction != 1)
			throw "Text paragraph movement arguments are invalid";
		var recordIndex = paragraphIndexAtOffset(offset);
		var record = paragraphs[recordIndex];
		var local = clamp(offset - record.start, 0, record.end - record.start);
		var paragraphEnd = macStyle || record.end >= offsets.codepointCount ? record.end : record.end + 1;
		if (direction < 0) {
			if (local > 0)
				return record.start;
			return recordIndex > 0 ? paragraphs[recordIndex - 1].start : 0;
		}
		if (local < record.end - record.start)
			return paragraphEnd;
		return recordIndex + 1 < paragraphs.length ? paragraphs[recordIndex + 1].start : record.end;
	}

	public function dispose():Void {
		if (disposed)
			return;
		for (record in paragraphs)
			record.layout.dispose();
		paragraphs = [];
		disposed = true;
	}

	function recomputeMetrics():Void {
		contentWidth = 0.0;
		contentHeight = 0.0;
		for (record in paragraphs) {
			record.y = contentHeight;
			var metrics = record.layout.measure();
			var lineHeight = paragraphStyle.lineHeight == null ? 0.0 : paragraphStyle.lineHeight;
			if (lineHeight <= 0.0) {
				var caret = record.layout.caret(new TextPosition(0, 0));
				lineHeight = Math.abs(caret.descender - caret.ascender);
			}
			record.height = Math.max(metrics.height, Math.max(1.0, lineHeight));
			contentWidth = Math.max(contentWidth, metrics.width);
			contentHeight += record.height;
		}
	}

	function paragraphAtY(y:Float):TextEditorParagraphRecord {
		if (y <= 0.0)
			return paragraphs[0];
		for (record in paragraphs)
			if (y < record.y + record.height)
				return record;
		return paragraphs[paragraphs.length - 1];
	}

	function paragraphAtOffset(offset:Int):TextEditorParagraphRecord
		return paragraphs[paragraphIndexAtOffset(offset)];

	function paragraphIndexAtOffset(offset:Int):Int {
		var value = clamp(offset, 0, offsets.codepointCount);
		var low = 0;
		var high = paragraphs.length - 1;
		var result = high;
		while (low <= high) {
			var middle = (low + high) >> 1;
			var record = paragraphs[middle];
			if (value < record.start)
				high = middle - 1;
			else if (value > record.end)
				low = middle + 1;
			else
				return middle;
		}
		return clamp(low, 0, paragraphs.length - 1);
	}

	function ensureLive():Void {
		if (disposed)
			throw "Editor layout has been disposed";
	}

	static function containsRecord(records:Array<TextEditorParagraphRecord>,
			value:TextEditorParagraphRecord):Bool {
		for (record in records)
			if (record == value)
				return true;
		return false;
	}

	static function copyTextStyle(style:TextStyle):TextStyle
		return new TextStyle(style.fontSize, style.font, style.letterSpacing);

	static function copyParagraphStyle(style:ParagraphStyle):ParagraphStyle
		return new ParagraphStyle(style.wrap, style.alignment, style.lineHeight, style.direction);

	static inline function clamp(value:Int, low:Int, high:Int):Int
		return value < low ? low : (value > high ? high : value);
}

class TextEditorParagraphRecord {
	public var start:Int;
	public var end:Int;
	public var text:String;
	public var y:Float;
	public var height:Float;
	public var graphemeBoundaries:Array<Int>;
	public final layout:TextLayout;

	public function new(text:String, layout:TextLayout) {
		this.text = text;
		this.layout = layout;
		start = 0;
		end = 0;
		y = 0.0;
		height = 0.0;
		graphemeBoundaries = layout.graphemeBoundaries();
	}
}
