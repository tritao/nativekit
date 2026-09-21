import haxe.io.Bytes;
import TextLayout.TextPosition;

class LayoutSessionSmoke {
	static function rejectsInvalidTextStyle(node:LayoutNode):Bool {
		try {
			LayoutTransaction.encode(node);
			return false;
		} catch (_:Dynamic) {
			return true;
		}
	}

	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 2;

		var fonts = FontCollection.create();
		fonts.add(fontPath);
		var infinity = Math.pow(2.0, 1024.0);
		var infiniteFontSize = LayoutNode.textNode(90, "font size");
		infiniteFontSize.textStyle.fontSize = infinity;
		var infiniteLetterSpacing = LayoutNode.textNode(91, "letter spacing");
		infiniteLetterSpacing.textStyle.letterSpacing = -infinity;
		var infiniteLineHeight = LayoutNode.textNode(92, "line height");
		infiniteLineHeight.paragraphStyle.lineHeight = infinity;
		if (!rejectsInvalidTextStyle(infiniteFontSize) ||
			!rejectsInvalidTextStyle(infiniteLetterSpacing) ||
			!rejectsInvalidTextStyle(infiniteLineHeight))
			return 5;
		var session = LayoutSession.create();
		session.setFonts(fonts);
		var textLayout = TextLayout.createStyled(fonts, "NativeKit selection", 220.0,
			new TextStyle(18.0), new ParagraphStyle());
		var selection = textLayout.selectionRects(new TextPosition(0, 0), new TextPosition(9, 0));
		var collapsedSelection = textLayout.selectionRects(new TextPosition(4, 0), new TextPosition(4, 0));
		if (selection.length == 0 || selection[0].width <= 0.0 ||
			collapsedSelection.length != 0 || textLayout.nextGrapheme(1) != 2 ||
			textLayout.previousGrapheme(2) != 1 || textLayout.alignGrapheme(2) != 2)
			return 6;
		textLayout.dispose();

		var root = LayoutNode.box(100);
		root.style.width = LayoutAxis.fixed(256.0);
		root.style.height = LayoutAxis.fixed(192.0);
		root.style.clipHorizontal = true;
		root.style.clipVertical = true;
		var panel = LayoutNode.box(101);
		panel.style.width = LayoutAxis.fixed(160.0);
		panel.style.height = LayoutAxis.fixed(64.0);
		panel.style.childAlignX = LayoutAlignmentX.Center;
		panel.style.childAlignY = LayoutAlignmentY.Center;
		panel.style.childDistribution = LayoutDistribution.Center;
		panel.style.wrapMode = LayoutWrapMode.Wrap;
		panel.style.rowGap = 6.5;
		panel.style.columnGap = 4.25;
		panel.style.transform = Transform2D.identity().translated(100.0, 20.0);
		panel.add(LayoutNode.textNode(102, "Press"));
		root.add(panel);
		var frame = new LayoutFrame(256.0, 192.0);

		var resolved = session.submit(root, frame);
		if (resolved.length != 3 || resolved[1].id != 101 || resolved[1].width != 160.0 ||
			!resolved[1].visible || resolved[1].transform.tx != 100.0 || resolved[1].transform.ty != 20.0 ||
			!resolved[1].hitTest(120.0, 30.0) || resolved[1].hitTest(258.0, 30.0) ||
			resolved[0].contentBounds.width != 160.0 || resolved[0].contentBounds.height != 64.0)
			return 3;
		frame.deltaSeconds = 1.0 / 60.0;
		resolved = session.submit(root, frame);
		if (resolved.length != 3 || resolved[2].id != 102 || resolved[2].width <= 0.0 ||
			resolved[2].transform.tx != 100.0 || resolved[2].clipBounds.width != 256.0 ||
			!resolved[2].hasBaseline ||
			resolved[2].x < resolved[1].x + (resolved[1].width - resolved[2].width) * 0.5 - 0.1 ||
			resolved[2].x > resolved[1].x + (resolved[1].width - resolved[2].width) * 0.5 + 0.1 ||
			resolved[2].y < resolved[1].y + (resolved[1].height - resolved[2].height) * 0.5 - 0.1 ||
			resolved[2].y > resolved[1].y + (resolved[1].height - resolved[2].height) * 0.5 + 0.1)
			return 4;
		var floatingText = panel.children[0];
		floatingText.style.positioning = LayoutPositioning.Absolute;
		floatingText.style.positionX = 20.0;
		floatingText.style.positionY = 10.0;
		floatingText.style.zIndex = 9;
		resolved = session.submit(root, frame);
		if (resolved[2].x != resolved[1].x + 20.0 || resolved[2].y != resolved[1].y + 10.0)
			return 7;

		var measureCalls = 0;
		var measureContent = new LayoutMeasuredContent(function(constraints:LayoutMeasureConstraints) {
			measureCalls++;
			if (constraints.maxWidth < constraints.minWidth || constraints.maxHeight < constraints.minHeight)
				throw "Invalid intrinsic measurement constraints";
			return new LayoutMeasureResult(48.0, 20.0, 15.0, true);
		});
		var measuredNode = LayoutNode.custom(103, measureContent);
		panel.add(measuredNode);
		resolved = session.submit(root, frame);
		var measuredItem:Null<ResolvedLayoutItem> = null;
		for (item in resolved)
			if (item.id == 103)
				measuredItem = item;
		var callsAfterFirst = measureCalls;
		if (callsAfterFirst == 0 || measuredItem == null || measuredItem.width != 48.0 ||
			measuredItem.height != 20.0 || !measuredItem.hasBaseline)
			return 41;
		resolved = session.submit(root, frame);
		if (measureCalls != callsAfterFirst)
			return 42;
		measureContent.invalidate();
		resolved = session.submit(root, frame);
		if (measureCalls <= callsAfterFirst)
			return 43;
		var stats = session.measureStats();
		if (stats.requests < 2 || stats.cacheHits == 0 || stats.cacheMisses == 0 ||
			stats.callbackCalls != measureCalls || stats.cacheEntries == 0 ||
			stats.cacheEntries > stats.cacheCapacity)
			return 46;

		var paintCalls = 0;
		var renderable = new LayoutRenderableContent(measureContent, function(canvas, geometry) {
			paintCalls++;
			canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
				Color.rgba(0.2, 0.4, 0.8, 1.0));
		});
		measuredNode.intrinsicContent = renderable;
		resolved = session.submit(root, frame);
		measuredItem = null;
		for (item in resolved)
			if (item.id == 103)
				measuredItem = item;
		if (measuredItem == null)
			return 47;
		var paintedList = renderable.paint(measuredItem);
		if (paintCalls != 1 || paintedList.info().commandCount == 0)
			return 48;
		renderable.paint(measuredItem);
		if (paintCalls != 1)
			return 49;
		measuredNode.style.transform = Transform2D.identity().translated(12.0, 6.0);
		resolved = session.submit(root, frame);
		if (paintCalls != 1)
			return 50;
		measuredNode.style.transform = Transform2D.identity();
		resolved = session.submit(root, frame);
		if (paintCalls != 1)
			return 51;
		measureContent.invalidate();
		renderable.paint(measuredItem);
		if (paintCalls != 2)
			return 52;
		renderable.dispose();

		var image = Image.create(8, 6, ImageFormat.RGBA8, Bytes.alloc(8 * 6 * 4));
		panel.add(LayoutNode.custom(104, new LayoutImageContent(image)));
		resolved = session.submit(root, frame);
		var imageItem:Null<ResolvedLayoutItem> = null;
		for (item in resolved)
			if (item.id == 104)
				imageItem = item;
		if (imageItem == null || imageItem.width != 8.0 || imageItem.height != 6.0)
			return 44;
		image.dispose();

		measuredNode.intrinsicContent = null;
		measuredNode.measureVersion = 2;
		var fallbackCalls = 0;
		session.setMeasureCallback(function(nodeId:Int, constraints:LayoutMeasureConstraints) {
			fallbackCalls++;
			if (nodeId != 103)
				throw "Unexpected fallback measurement request";
			return new LayoutMeasureResult(32.0, 12.0);
		});
		resolved = session.submit(root, frame);
		measuredItem = null;
		for (item in resolved)
			if (item.id == 103)
				measuredItem = item;
		if (fallbackCalls == 0 || measuredItem == null || measuredItem.width != 32.0 ||
			measuredItem.height != 12.0)
			return 45;
		session.setMeasureCallback(null);

		session.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxeon layout session transaction");
		return 0;
	}
}
