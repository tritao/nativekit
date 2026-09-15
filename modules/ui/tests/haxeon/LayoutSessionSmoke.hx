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
		panel.style.childAlignX = LayoutAlignment.Center;
		panel.style.childAlignY = LayoutAlignment.Center;
		panel.style.childDistribution = LayoutDistribution.Center;
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

		session.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxeon layout session transaction");
		return 0;
	}
}
