import FontCollection;
import LayoutAxis;
import LayoutFrame;
import LayoutSession;
import LayoutStyle;
import nativekit.ui.core.UiContext;
import nativekit.ui.debug.UiFrameMetrics;
import nativekit.ui.core.View;
import nativekit.ui.widgets.ListView;
import nativekit.ui.widgets.ListViewModel;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TreeView;
import nativekit.ui.widgets.TreeViewModel;
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
		var valid = run(fonts, 10000) && run(fonts, 100000) &&
			runModel(fonts, 10000) && runModel(fonts, 100000) &&
			runTree(fonts, 100000) &&
			runSubmitComparison(fonts, 10000) &&
			runSubmitComparison(fonts, 100000) &&
			runHitTestBenchmarks();
		fonts.dispose();
		return valid ? 0 : 1;
	}

	/** Compares rebuilding the same virtual tree with retained submission reuse. */
	static function runSubmitComparison(fonts:FontCollection, itemCount:Int):Bool {
		var frame = new LayoutFrame(viewportWidth, viewportHeight);
		var fullController = new ScrollController();
		var fullStyle = new LayoutStyle();
		fullStyle.width = LayoutAxis.fixed(viewportWidth);
		fullStyle.height = LayoutAxis.fixed(viewportHeight);
		var fullRows:Array<Int> = [];
		var fullContext = new UiContext(LayoutSession.create(), fonts);
		var fullList = new VirtualList('submit-full-$itemCount', itemCount, itemHeight,
			function(index) {
				fullRows.push(index);
				return new Text('Row $index');
			}, fullStyle, null, fullController, viewportHeight);
		fullContext.submit(fullList, frame);
		var fullSeconds = 0.0;
		var fullNodes = 0;
		for (_ in 0...samples) {
			fullRows.resize(0);
			var started = Sys.time();
			fullContext.submit(fullList, frame);
			fullSeconds += Sys.time() - started;
			var metrics:Null<UiFrameMetrics> = fullContext.frameMetrics;
			if (metrics == null || metrics.reusedSubmission || metrics.nodeCount <= 0)
				return false;
			fullNodes += metrics.nodeCount;
		}
		fullContext.dispose();

		var retainedController = new ScrollController();
		var retainedStyle = new LayoutStyle();
		retainedStyle.width = LayoutAxis.fixed(viewportWidth);
		retainedStyle.height = LayoutAxis.fixed(viewportHeight);
		var retainedRows:Array<Int> = [];
		var retainedContext = new UiContext(LayoutSession.create(), fonts);
		var retainedList = new VirtualList('submit-retained-$itemCount', itemCount, itemHeight,
			function(index) {
				retainedRows.push(index);
				return new Text('Row $index');
			}, retainedStyle, null, retainedController, viewportHeight);
		retainedContext.submitCached(function() return retainedList, frame,
			'submit-retained-$itemCount');
		var retainedSeconds = 0.0;
		var retainedNodes = 0;
		var retainedFrames = 0;
		for (_ in 0...samples) {
			retainedRows.resize(0);
			var started = Sys.time();
			retainedContext.submitCached(function() return retainedList, frame,
				'submit-retained-$itemCount');
			retainedSeconds += Sys.time() - started;
			var metrics:Null<UiFrameMetrics> = retainedContext.frameMetrics;
			if (metrics == null || !metrics.reusedSubmission || !metrics.nativeLayoutReused ||
				metrics.nodeCount <= 0 || retainedRows.length != 0)
				return false;
			retainedNodes += metrics.nodeCount;
			retainedFrames++;
		}
		retainedContext.dispose();

		var fullMicros = fullSeconds * 1000000.0 / samples;
		var retainedMicros = retainedSeconds * 1000000.0 / samples;
		var speedup = retainedMicros <= 0.0 ? 0.0 : fullMicros / retainedMicros;
		Sys.println('submit_compare items=$itemCount full_us=$fullMicros retained_us=$retainedMicros ' +
			'speedup=$speedup full_nodes=${fullNodes / samples} retained_nodes=${retainedNodes / samples} ' +
			'retained_frames=$retainedFrames');
		return retainedFrames == samples;
	}

	/** Measures native geometric picking and the Haxe hit-path boundary. */
	static function runHitTestBenchmarks():Bool {
		return runHitScenario('ordinary_1k', ordinaryTree(1000), 0, 256) &&
			runHitScenario('ordinary_10k', ordinaryTree(10000), 0, 256) &&
			runHitScenario('deeply_nested_clipping', clippedTree(32), 1, 256) &&
			runHitScenario('pointer_sweep_10k', ordinaryTree(10000), 3, 1024);
	}

	static function runHitScenario(name:String, root:LayoutNode, mode:Int,
			queryCount:Int):Bool {
		var session = LayoutSession.create();
		var frame = new LayoutFrame(viewportWidth, viewportHeight);
		session.submit(root, frame);
		var path:Array<Int> = [];
		var checksum = 0;
		var started = Sys.time();
		for (sample in 0...queryCount) {
			var x:Float;
			var y:Float;
			switch (mode) {
			case 1:
				// Alternate between the center of the clipping chain and a point
				// just outside the inner viewport.
				x = sample % 2 == 0 ? viewportWidth * 0.5 : viewportWidth - 2.0;
				y = viewportHeight * 0.5;
			default:
				var columns = mode == 3 ? 64 : 32;
				var rows = mode == 3 ? Std.int(Math.ceil(queryCount / columns)) : 8;
				var column = sample % columns;
				var row = Std.int(sample / columns) % rows;
				x = (column + 0.5) * viewportWidth / columns;
				y = (row + 0.5) * viewportHeight / rows;
			}
			session.hitTestInto(x, y, path);
			checksum += path.length;
		}
		var boundaryMicros = (Sys.time() - started) * 1000000.0 / queryCount;
		var stats = session.hitTestStats();
		var hits = haxe.Int64.toInt(stats.hitTestCount);
		var visited = haxe.Int64.toInt(stats.nodesVisited);
		var rejected = haxe.Int64.toInt(stats.subtreesRejected);
		var precise = haxe.Int64.toInt(stats.preciseHitTests);
		var maxVisited = haxe.Int64.toInt(stats.maxNodesVisited);
		var nativeMicros = haxe.Int64.toInt(stats.hitTestTimeNanoseconds) / 1000.0 /
			Math.max(1, hits);
		var averageVisited = visited / Math.max(1, hits);
		var averagePrecise = precise / Math.max(1, hits);
		var rejectionRatio = rejected / Math.max(1, visited);
		Sys.println('hit_test name=$name queries=$hits ' +
			'avg_nodes_visited=$averageVisited subtree_rejection_ratio=$rejectionRatio ' +
			'avg_precise_tests=$averagePrecise native_us=$nativeMicros ' +
			'boundary_us=$boundaryMicros max_nodes_visited=$maxVisited checksum=$checksum');
		session.dispose();
		return hits == queryCount && checksum > 0;
	}

	static function fixedStyle(width:Float, height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(width);
		style.height = LayoutAxis.fixed(height);
		return style;
	}

	static function ordinaryTree(itemCount:Int):LayoutNode {
		var rootStyle = fixedStyle(viewportWidth, viewportHeight);
		rootStyle.clipHorizontal = true;
		rootStyle.clipVertical = true;
		var root = LayoutNode.box(1, rootStyle);
		var groupSize = 32;
		var groupCount = Std.int(Math.ceil(itemCount / groupSize));
		var nextId = 2;
		for (groupIndex in 0...groupCount) {
			var rowCount = Std.int(Math.min(groupSize, itemCount - groupIndex * groupSize));
			var group = LayoutNode.box(nextId++, fixedStyle(viewportWidth, rowCount * itemHeight));
			group.hitSelf = false;
			for (_ in 0...rowCount)
				group.add(LayoutNode.box(nextId++, fixedStyle(viewportWidth, itemHeight)));
			root.add(group);
		}
		return root;
	}

	static function clippedTree(depth:Int):LayoutNode {
		var rootStyle = fixedStyle(viewportWidth, viewportHeight);
		rootStyle.clipHorizontal = true;
		rootStyle.clipVertical = true;
		var root = LayoutNode.box(1, rootStyle);
		var parent = root;
		for (index in 0...depth) {
			var style = fixedStyle(viewportWidth - 8.0, viewportHeight - 8.0);
			style.clipHorizontal = true;
			style.clipVertical = true;
			var child = LayoutNode.box(index + 2, style);
			parent.add(child);
			parent = child;
		}
		return root;
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

	static function runModel(fonts:FontCollection, itemCount:Int):Bool {
		var controller = new ScrollController();
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(viewportWidth);
		style.height = LayoutAxis.fixed(viewportHeight);
		var builtRows:Array<Int> = [];
		var model = new BenchmarkListModel(itemCount, builtRows);
		var context = new UiContext(LayoutSession.create(), fonts);
		var list = new ListView('model-list-$itemCount', model, style, controller,
			viewportHeight);
		var frame = new LayoutFrame(viewportWidth, viewportHeight);
		var firstStart = Sys.time();
		context.submit(list, frame);
		var firstSeconds = Sys.time() - firstStart;
		var firstRows = builtRows.length;
		var maxExpectedRows = Std.int(Math.ceil(viewportHeight / 20.0)) + 4;
		var valid = firstRows > 0 && firstRows <= maxExpectedRows &&
			model.extentCalls > 0 && model.extentCalls < itemCount;
		var extentCallsBeforeUpdate = model.extentCalls;
		model.bumpExtent(Std.int(itemCount / 2));
		builtRows.resize(0);
		context.submit(list, frame);
		if (model.extentCalls != extentCallsBeforeUpdate)
			valid = false;

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
		Sys.println('model_items=$itemCount first_rows=$firstRows ' +
			'avg_rows=$averageRows max_rows=$maxRows ' +
			'avg_nodes=$averageNodes max_nodes=$maxNodes ' +
			'extent_calls=${model.extentCalls} extent_reuses=${list.extentReuses} ' +
			'first_us=${firstSeconds * 1000000.0} ' +
			'avg_us=$averageMicros');

		context.dispose();
		return valid && model.extentCalls < itemCount;
	}

	static function runTree(fonts:FontCollection, itemCount:Int):Bool {
		var controller = new ScrollController();
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(viewportWidth);
		style.height = LayoutAxis.fixed(viewportHeight);
		var builtKeys:Array<String> = [];
		var model = new BenchmarkTreeModel(itemCount, builtKeys);
		var context = new UiContext(LayoutSession.create(), fonts);
		var tree = new TreeView('tree-$itemCount', model, style, controller, viewportHeight);
		var frame = new LayoutFrame(viewportWidth, viewportHeight);
		var firstStart = Sys.time();
		context.submit(tree, frame);
		var firstSeconds = Sys.time() - firstStart;
		var firstRows = builtKeys.length;
		var maxExpectedRows = Std.int(Math.ceil(viewportHeight / 24.0)) + 4;
	var valid = firstRows > 0 && firstRows <= maxExpectedRows &&
			model.extentCalls > 0 && model.extentCalls < itemCount;

		var totalSeconds = 0.0;
		var totalRows = 0;
		var totalNodes = 0;
		var maxRows = 0;
		var maxNodes = 0;
		var maxScroll = controller.maxScrollY;
		for (sample in 0...samples) {
			var fraction = samples <= 1 ? 0.0 : sample / (samples - 1);
			controller.jumpTo(0.0, maxScroll * fraction);
			builtKeys.resize(0);
			var start = Sys.time();
			context.submit(tree, frame);
			var elapsed = Sys.time() - start;
			var rowCount = builtKeys.length;
			var frameMetrics = context.frameMetrics;
			var nodeCount = frameMetrics == null ? 0 : frameMetrics.nodeCount;
			if (rowCount == 0 || rowCount > maxExpectedRows || nodeCount <= 0)
				valid = false;
			if (fraction == 0.0 && (builtKeys.length == 0 || builtKeys[0] != "root:0"))
				valid = false;
			if (fraction == 1.0 &&
				(builtKeys.length == 0 || builtKeys[builtKeys.length - 1] != 'root:${itemCount - 1}'))
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
		Sys.println('tree_items=$itemCount first_rows=$firstRows ' +
			'avg_rows=$averageRows max_rows=$maxRows ' +
			'avg_nodes=$averageNodes max_nodes=$maxNodes ' +
			'extent_calls=${model.extentCalls} child_calls=${model.childCalls} ' +
			'first_us=${firstSeconds * 1000000.0} avg_us=$averageMicros');

		context.dispose();
		return valid && model.extentCalls < itemCount;
	}
}

private class BenchmarkListModel implements ListViewModel {
	final itemCount:Int;
	final builtRows:Array<Int>;
	public var extentCalls:Int;
	var modelRevision:Int;
	var extentRevisionValue:Int;
	var changedExtentIndex:Int;

	public function new(itemCount:Int, builtRows:Array<Int>) {
		this.itemCount = itemCount;
		this.builtRows = builtRows;
		extentCalls = 0;
		modelRevision = 1;
		extentRevisionValue = 1;
		changedExtentIndex = -1;
	}

	public function count():Int
		return itemCount;

	public function keyAt(index:Int):String
		return 'benchmark-item:$index';

	public function estimatedExtent():Float
		return 24.0;

	public function extentIsUniform():Bool
		return false;

	public function extentAt(index:Int):Float {
		extentCalls++;
		return 20.0 + (index % 3) * 4.0 + (index == changedExtentIndex ? 4.0 : 0.0);
	}

	public function extentRevisionAt(index:Int):Int
		return index == changedExtentIndex ? extentRevisionValue : 1;

	public function bumpExtent(index:Int):Void {
		changedExtentIndex = index;
		extentRevisionValue++;
		modelRevision++;
	}

	public function buildItem(index:Int):View {
		builtRows.push(index);
		return new Text('Row $index');
	}

	public function revision():Int
		return modelRevision;
}

private class BenchmarkTreeModel implements TreeViewModel {
	final itemCount:Int;
	final builtKeys:Array<String>;
	public var extentCalls:Int;
	public var childCalls:Int;

	public function new(itemCount:Int, builtKeys:Array<String>) {
		this.itemCount = itemCount;
		this.builtKeys = builtKeys;
		extentCalls = 0;
		childCalls = 0;
	}

	public function rootCount():Int
		return itemCount;

	public function rootKeyAt(index:Int):String
		return 'root:$index';

	public function childCount(parentKey:String):Int {
		childCalls++;
		return 0;
	}

	public function childKeyAt(parentKey:String, index:Int):String
		return '$parentKey:child:$index';

	public function initiallyExpanded(key:String):Bool
		return false;

	public function estimatedExtent():Float
		return 28.0;

	public function extentIsUniform():Bool
		return false;

	public function extentAt(key:String):Float {
		extentCalls++;
		var separator = key.indexOf(":");
		var index:Int = cast Std.parseInt(key.substr(separator + 1));
		return 24.0 + (index % 2) * 8.0;
	}

	public function buildItem(key:String):View {
		builtKeys.push(key);
		return new Text(key);
	}

	public function revision():Int
		return 1;
}
