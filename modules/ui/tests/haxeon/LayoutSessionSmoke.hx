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
		var button = LayoutNode.button(101);
		button.style.width = LayoutAxis.fixed(160.0);
		button.style.height = LayoutAxis.fixed(64.0);
		button.add(LayoutNode.textNode(102, "Press"));
		root.add(button);
		var frame = new LayoutFrame(256.0, 192.0);

		if (session.submit(root, frame).length != 0)
			return 3;
		frame.setPointer(8.0, 8.0, true);
		frame.deltaSeconds = 1.0 / 60.0;
		session.submit(root, frame);
		frame.pointerDown = false;
		var events = session.submit(root, frame);
		if (events.length != 1 || !events[0].isButtonActivated() || events[0].nodeId != 101)
			return 4;

		session.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxeon layout session transaction");
		return 0;
	}
}
