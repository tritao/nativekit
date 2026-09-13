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

		var root = LayoutNode.box(100);
		root.style.width = LayoutAxis.fixed(256.0);
		root.style.height = LayoutAxis.fixed(192.0);
		var panel = LayoutNode.box(101);
		panel.style.width = LayoutAxis.fixed(160.0);
		panel.style.height = LayoutAxis.fixed(64.0);
		panel.add(LayoutNode.textNode(102, "Press"));
		root.add(panel);
		var frame = new LayoutFrame(256.0, 192.0);

		var resolved = session.submit(root, frame);
		if (resolved.length != 3 || resolved[1].id != 101 || resolved[1].width != 160.0)
			return 3;
		frame.deltaSeconds = 1.0 / 60.0;
		resolved = session.submit(root, frame);
		if (resolved.length != 3 || resolved[2].id != 102 || resolved[2].width <= 0.0)
			return 4;

		session.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxeon layout session transaction");
		return 0;
	}
}
