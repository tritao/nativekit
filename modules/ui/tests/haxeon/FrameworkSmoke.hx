import Color;
import FontCollection;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutStyle;
import nativekit.ui.core.State;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Text;

class FrameworkSmoke {
	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 2;
		var fonts = FontCollection.create();
		fonts.add(fontPath);
		var session = LayoutSession.create();
		session.setFonts(fonts);
		var context = new UiContext(session);
		var frame = new LayoutFrame(256.0, 192.0);
		var clicks = 0;
		var bubbled = 0;
		var captured = 0;

		function makeView(buttonEnabled:Bool = true):Column {
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(256.0);
			style.height = LayoutAxis.fixed(192.0);
			var button = new Button("Increment", null, function() {
				clicks++;
			});
			button.enabled = buttonEnabled;
			return new Column("main", [
				new KeyedView("increment", button),
				new KeyedView("status", new Text("Ready", null, Color.rgba(1.0, 1.0, 1.0, 1.0)))
			], style);
		}

		var root = context.submit(makeView(true), frame);
		root.on(UiEventKind.Click, function(_) {
			bubbled++;
		});
		root.on(UiEventKind.Click, function(_) {
			captured++;
		}, "capture");
		var buttonNode = root.children[0];
		var initialId = buttonNode.id;
		var state:State<Int> = context.buildContext.state(buttonNode.id, 0);
		if (root.children.length != 2 || buttonNode.resolved == null ||
			!buttonNode.focusable || !buttonNode.resolved.hitTest(4.0, 4.0) || context.isDirty())
			return 3;

		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(4.0, 4.0, 0);
		if (clicks != 1 || bubbled != 1 || captured != 1 || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(initialId))
			return 4;
		state.update(7);
		if (!context.isDirty())
			return 5;

		root = context.submit(makeView(true), frame);
		buttonNode = root.children[0];
		var restored = context.buildContext.state(buttonNode.id, 99);
		if (!buttonNode.id.equals(initialId) || restored.value != 7 ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(initialId))
			return 6;

		context.clearFocus();
		context.focusNext();
		if (context.focus.focusedId == null || !context.focus.focusedId.equals(initialId))
			return 7;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(500.0, 500.0, 0);
		if (clicks != 1)
			return 8;

		var sharedStyle = new LayoutStyle();
		new Row("style-copy", [], sharedStyle);
		if (sharedStyle.direction != LayoutDirection.TopToBottom)
			return 9;

		root = context.submit(makeView(false), frame);
		buttonNode = root.children[0];
		if (context.focus.focusedId != null || context.focusWidget(buttonNode.id))
			return 10;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(4.0, 4.0, 0);
		if (clicks != 1)
			return 11;

		context.dispose();
		fonts.dispose();
		Sys.println("PASS: Haxe views, stable IDs, state, events, hit testing, and focus");
		return 0;
	}
}
