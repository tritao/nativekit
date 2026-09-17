import haxe.io.Bytes;
import nativekit.ui.widgets.TextEditorHistoryKind;
import nativekit.ui.widgets.TextEditorState;
import nativekit.ui.widgets.TextOffsetMap;

class TextEditorBenchmark {
	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 2;

		var sizes = [1024, 100 * 1024, 1024 * 1024, 10 * 1024 * 1024];
		Sys.println('{"benchmark":"haxe_text_editor"}');
		for (targetBytes in sizes) {
			var fonts = FontCollection.create();
			fonts.add(fontPath);
			var document = makeDocument(targetBytes);
			var textStyle = new TextStyle(16.0);
			var paragraphStyle = new ParagraphStyle(TextWrap.WordCharacter);

			var constructStart = Sys.time();
			var editor = new TextEditorState(fonts, document, textStyle, paragraphStyle);
			var constructUs = elapsedUs(constructStart);

			var layoutStart = Sys.time();
			editor.updateLayout(640.0);
			var layoutUs = elapsedUs(layoutStart);

			var map = editor.documentOffsets();
			var edit = findEditOffset(map);
			var replacement = "é";
			var replacementEnd = edit + 1;

			var transactionStart = Sys.time();
			if (!editor.replaceRange(edit, replacementEnd, replacement,
				TextEditorHistoryKind.Generic))
				return fail(editor, fonts, 3);
			var transactionUs = elapsedUs(transactionStart);

			var geometryStart = Sys.time();
			var selectionEnd = Std.int(Math.min(editor.documentLength(), edit + 8));
			editor.setSelection(edit, selectionEnd);
			var caret = editor.layout.caret(editor.focusPosition());
			var selectionRects = editor.layout.selectionRangeRects(editor.anchorPosition(),
				editor.focusPosition());
			var hit = editor.layout.hitTest(caret.x, caret.y);
			var line = editor.layout.lineRangeAt(edit);
			editor.ensureCaretVisible(240.0);
			var geometryUs = elapsedUs(geometryStart);

			var mappingStart = Sys.time();
			var documentMap = editor.documentOffsets();
			var utf8 = documentMap.utf8OffsetForCodepoint(edit);
			var utf16 = documentMap.utf16OffsetForCodepoint(edit);
			var codepointFromUtf8 = documentMap.codepointOffsetForUtf8(utf8);
			var codepointFromUtf16 = documentMap.codepointOffsetForUtf16(utf16);
			var activeMap = editor.activeParagraphOffsets();
			var activeRange = editor.activeParagraphRange();
			var activeUtf16 = activeMap.utf16OffsetForCodepoint(
				Std.int(Math.min(activeMap.codepointCount, 1)));
			var mappingUs = elapsedUs(mappingStart);

			var undoStart = Sys.time();
			if (!editor.undo())
				return fail(editor, fonts, 4);
			var undoUs = elapsedUs(undoStart);

			var redoStart = Sys.time();
			if (!editor.redo())
				return fail(editor, fonts, 5);
			var redoUs = elapsedUs(redoStart);

			var checksum = caret.x + caret.y + caret.ascender + caret.descender +
				selectionRects.length + hit.offset + hit.affinity + line.start + line.end +
				codepointFromUtf8 + codepointFromUtf16 + activeRange.start + activeRange.end +
				activeUtf16;
			if (!Math.isFinite(checksum))
				return fail(editor, fonts, 6);

			Sys.println('{"target_bytes":${targetBytes},' +
				'"document_bytes":${Bytes.ofString(document).length},' +
				'"codepoint_count":${editor.documentLength()},' +
				'"paragraph_count":${editor.layout.paragraphCount},' +
				'"construct_us":${format(constructUs)},' +
				'"layout_us":${format(layoutUs)},' +
				'"transaction_us":${format(transactionUs)},' +
				'"geometry_us":${format(geometryUs)},' +
				'"mapping_us":${format(mappingUs)},' +
				'"undo_us":${format(undoUs)},' +
				'"redo_us":${format(redoUs)}}');

			editor.dispose();
			fonts.dispose();
		}
		return 0;
	}

	static function findEditOffset(map:TextOffsetMap):Int {
		var middle = map.codepointCount >> 1;
		for (distance in 0...(map.codepointCount + 1)) {
			var right = middle + distance;
			if (right < map.codepointCount) {
				var value = map.sliceCodepoints(right, right + 1);
				if (value != "\n" && value != "\r")
					return right;
			}
			if (middle >= distance) {
				var left = middle - distance;
				if (left < map.codepointCount) {
					var value = map.sliceCodepoints(left, left + 1);
					if (value != "\n" && value != "\r")
						return left;
				}
			}
		}
		return 0;
	}

	static function makeDocument(targetBytes:Int):String {
		var totalBytes = 0;
		var chunks:Array<String> = [];
		var paragraphNumber = 0;
		while (totalBytes < targetBytes) {
			var seed = "NativeKit editor paragraph ${paragraphNumber}: text shaping, " +
				"IME composition, Unicode selection, and retained layout. 日本語 مرحبا שלום " +
				"👨‍👩‍👧‍👦 ";
			var bodyLimit = Std.int(Math.max(1.0,
				Math.min(4095.0, targetBytes - totalBytes - 1.0)));
			var paragraph = "";
			while (Bytes.ofString(paragraph).length + Bytes.ofString(seed).length <= bodyLimit)
				paragraph += seed;
			if (paragraph.length == 0)
				paragraph = seed;
			paragraph += "\n";
			chunks.push(paragraph);
			totalBytes += Bytes.ofString(paragraph).length;
			paragraphNumber++;
		}
		return chunks.join("");
	}

	static function elapsedUs(start:Float):Float
		return (Sys.time() - start) * 1000000.0;

	static function format(value:Float):String
		return Std.string(Math.round(value * 100.0) / 100.0);

	static function fail(editor:TextEditorState, fonts:FontCollection, code:Int):Int {
		editor.dispose();
		fonts.dispose();
		return code;
	}
}
