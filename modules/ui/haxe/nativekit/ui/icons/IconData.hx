package nativekit.ui.icons;

import Path;
import PathBuilder;

/** Generated 24×24 vector paths derived from the vendored Lucide SVG assets. */
class IconData {
	public static function build(name:IconName):Path {
		return switch name {
			case IconName.Search: search();
			case IconName.Close: close();
			case _: throw "Unsupported icon name";
		};
	}

	static function search():Path {
		var k = 4.4182779984;
		return new PathBuilder()
			.moveTo(21.0, 21.0).lineTo(16.66, 16.66)
			.moveTo(19.0, 11.0)
			.cubicTo(19.0, 11.0 + k, 11.0 + k, 19.0, 11.0, 19.0)
			.cubicTo(11.0 - k, 19.0, 3.0, 11.0 + k, 3.0, 11.0)
			.cubicTo(3.0, 11.0 - k, 11.0 - k, 3.0, 11.0, 3.0)
			.cubicTo(11.0 + k, 3.0, 19.0, 11.0 - k, 19.0, 11.0)
			.build();
	}

	static function close():Path
		return new PathBuilder().moveTo(18.0, 6.0).lineTo(6.0, 18.0)
			.moveTo(6.0, 6.0).lineTo(18.0, 18.0).build();
}
