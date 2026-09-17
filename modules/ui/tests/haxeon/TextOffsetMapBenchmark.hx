import haxe.io.Bytes;
import nativekit.ui.widgets.TextOffsetMap;

class TextOffsetMapBenchmark {
	static function main():Int {
		var sizes = [1024, 100 * 1024, 1024 * 1024, 10 * 1024 * 1024];
		Sys.println('{"benchmark":"haxe_text_offset_map"}');
		for (targetBytes in sizes) {
			var document = makeDocument(targetBytes);
			var buildStart = Sys.time();
			var map = new TextOffsetMap(document);
			var buildUs = (Sys.time() - buildStart) * 1000000.0;
			var edit = map.codepointCount >> 1;
			if (edit >= map.codepointCount)
				edit = 0;
			if (!map.isGraphemeBoundary(edit))
				edit = map.previousGraphemeBoundary(edit);
			var next = map.replaceCodepoints(edit, edit + 1, "é");

			var incrementalStart = Sys.time();
			map.replaceCodepointsIncremental(edit, edit + 1, "é", next);
			var incrementalUs = (Sys.time() - incrementalStart) * 1000000.0;

			var freshStart = Sys.time();
			var fresh = new TextOffsetMap(next);
			var freshUs = (Sys.time() - freshStart) * 1000000.0;
			if (map.text != fresh.text || map.codepointCount != fresh.codepointCount ||
				map.utf8ByteLength != fresh.utf8ByteLength || map.utf16Length != fresh.utf16Length ||
				map.paragraphCount() != fresh.paragraphCount() ||
				map.graphemeBoundaryCount() != fresh.graphemeBoundaryCount())
				return 1;
			Sys.println('{"target_bytes":${targetBytes},"document_bytes":${Bytes.ofString(document).length},' +
				'"codepoint_count":${map.codepointCount},"map_build_us":${format(buildUs)},' +
				'"incremental_edit_us":${format(incrementalUs)},"fresh_rebuild_us":${format(freshUs)}}');
		}
		return 0;
	}

	static function makeDocument(targetBytes:Int):String {
		var seed = "NativeKit text map: é 👨‍👩‍👧‍👦 🇺🇸 日本語 مرحبا שלום क्‍ष\n";
		var count = Std.int(Math.ceil(targetBytes / Bytes.ofString(seed).length));
		var chunks:Array<String> = [];
		for (_ in 0...count)
			chunks.push(seed);
		return chunks.join("");
	}

	static function format(value:Float):String
		return Std.string(Math.round(value * 100.0) / 100.0);
}
