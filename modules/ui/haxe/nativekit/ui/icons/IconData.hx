package nativekit.ui.icons;

import Path;
import PathBuilder;

/** Generated 24×24 vector paths derived from the vendored Lucide SVG assets. */
class IconData {
	public static function build(name:IconName):Path {
		return switch name {
			case IconName.Search: search();
			case IconName.Close: close();
			case IconName.Sun: sun();
			case IconName.Moon: moon();
			case IconName.Inspect: inspect();
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

	static function sun():Path {
		var k = 2.2091389992;
		return new PathBuilder()
			.moveTo(16.0, 12.0)
			.cubicTo(16.0, 12.0 + k, 12.0 + k, 16.0, 12.0, 16.0)
			.cubicTo(12.0 - k, 16.0, 8.0, 12.0 + k, 8.0, 12.0)
			.cubicTo(8.0, 12.0 - k, 12.0 - k, 8.0, 12.0, 8.0)
			.cubicTo(12.0 + k, 8.0, 16.0, 12.0 - k, 16.0, 12.0)
			.moveTo(12.0, 2.0).lineTo(12.0, 4.0)
			.moveTo(12.0, 20.0).lineTo(12.0, 22.0)
			.moveTo(4.93, 4.93).lineTo(6.34, 6.34)
			.moveTo(17.66, 17.66).lineTo(19.07, 19.07)
			.moveTo(2.0, 12.0).lineTo(4.0, 12.0)
			.moveTo(20.0, 12.0).lineTo(22.0, 12.0)
			.moveTo(4.93, 19.07).lineTo(6.34, 17.66)
			.moveTo(17.66, 6.34).lineTo(19.07, 4.93).build();
	}

	static function moon():Path
		return new PathBuilder()
			.moveTo(21.0, 12.79)
			.cubicTo(18.7, 14.05, 15.9, 13.95, 13.72, 12.35)
			.cubicTo(11.54, 10.75, 10.45, 8.14, 10.9, 5.55)
			.cubicTo(11.08, 4.5, 11.45, 3.5, 12.0, 2.62)
			.cubicTo(7.15, 2.8, 3.25, 6.78, 3.25, 11.68)
			.cubicTo(3.25, 16.7, 7.32, 20.77, 12.34, 20.77)
			.cubicTo(16.0, 20.77, 19.3, 18.57, 20.72, 15.2)
			.cubicTo(21.05, 14.42, 21.15, 13.6, 21.0, 12.79).build();

	static function inspect():Path
		return new PathBuilder()
			.moveTo(9.0, 3.0).lineTo(5.0, 3.0).lineTo(5.0, 7.0)
			.moveTo(15.0, 3.0).lineTo(19.0, 3.0).lineTo(19.0, 7.0)
			.moveTo(9.0, 21.0).lineTo(5.0, 21.0).lineTo(5.0, 17.0)
			.moveTo(15.0, 21.0).lineTo(19.0, 21.0).lineTo(19.0, 17.0)
			.moveTo(12.0, 8.0).lineTo(12.0, 16.0)
			.moveTo(8.0, 12.0).lineTo(16.0, 12.0).build();
}
