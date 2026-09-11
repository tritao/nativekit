class LayoutSessionSmoke {
	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 2;

		var fonts = FontCollection.create();
		fonts.add(fontPath);
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

		if (session.submit(root, 256.0, 192.0).length != 0)
			return 3;
		session.submit(root, 256.0, 192.0, 8.0, 8.0, true, 1.0 / 60.0);
		var events = session.submit(root, 256.0, 192.0, 8.0, 8.0, false, 1.0 / 60.0);
		if (events.length != 1 || !events[0].isButtonActivated() || events[0].nodeId != 101)
			return 4;

		session.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxeon layout session transaction");
		return 0;
	}
}
