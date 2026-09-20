import FontCollection;
import LayoutAxis;
import LayoutFrame;
import LayoutSession;
import LayoutStyle;
import nativekit.ui.core.UiContext;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.VirtualList;

/** Measures the Haxe virtual-list boundary without materializing the dataset. */
class VirtualListBenchmark {
	static inline var viewportWidth:Float = 256.0;
	static inline var viewportHeight:Float = 350.0;
	static inline var itemHeight:Float = 32.0;
	static inline var samples:Int = 32;

	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null) {
			Sys.println("virtual-list: NKUI_TEST_FONT_PATH is required");
			return 2;
		}

		var fonts = FontCollection.create();
		fonts.add(fontPath);
		var valid = run(fonts, 10000) && run(fonts, 100000);
		fonts.dispose();
		return valid ? 0 : 1;
	}

	static function run(fonts:FontCollection, itemCount:Int):Bool {
		var controller = new ScrollController();
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(viewportWidth);
		style.height = LayoutAxis.fixed(viewportHeight);
		var builtRows:Array<Int> = [];
		var context = new UiContext(LayoutSession.create(), fonts);
		var list = new VirtualList('benchmark-$itemCount', itemCount, itemHeight,
			function(index) {
				builtRows.push(index);
				return new Text('Row $index');
			}, style, null, controller, viewportHeight);
		var frame = new LayoutFrame(viewportWidth, viewportHeight);
		var maxExpectedRows = Std.int(Math.ceil(viewportHeight / itemHeight)) + 4;
		var firstStart = Sys.time();
		context.submit(list, frame);
		var firstSeconds = Sys.time() - firstStart;
		var firstRows = builtRows.length;
		var valid = firstRows > 0 && firstRows <= maxExpectedRows;

		var totalSeconds = 0.0;
		var totalRows = 0;
		var totalNodes = 0;
		var maxRows = 0;
		var maxNodes = 0;
		var maxScroll = controller.maxScrollY;
		for (sample in 0...samples) {
			var fraction = samples <= 1 ? 0.0 : sample / (samples - 1);
			controller.jumpTo(0.0, maxScroll * fraction);
			builtRows.resize(0);
			var start = Sys.time();
			context.submit(list, frame);
			var elapsed = Sys.time() - start;
			var rowCount = builtRows.length;
			var frameMetrics = context.frameMetrics;
			var nodeCount = frameMetrics == null ? 0 : frameMetrics.nodeCount;
			if (rowCount == 0 || rowCount > maxExpectedRows || nodeCount <= 0)
				valid = false;
			if (fraction == 0.0 && (builtRows.length == 0 || builtRows[0] != 0))
				valid = false;
			if (fraction == 1.0 &&
				(builtRows.length == 0 || builtRows[builtRows.length - 1] != itemCount - 1))
				valid = false;
			totalSeconds += elapsed;
			totalRows += rowCount;
			totalNodes += nodeCount;
			if (rowCount > maxRows)
				maxRows = rowCount;
			if (nodeCount > maxNodes)
				maxNodes = nodeCount;
		}

		var averageRows = totalRows / samples;
		var averageNodes = totalNodes / samples;
		var averageMicros = totalSeconds * 1000000.0 / samples;
		Sys.println('items=$itemCount first_rows=$firstRows ' +
			'avg_rows=$averageRows max_rows=$maxRows ' +
			'avg_nodes=$averageNodes max_nodes=$maxNodes ' +
			'first_us=${firstSeconds * 1000000.0} avg_us=$averageMicros');

		context.dispose();
		return valid;
	}
}
