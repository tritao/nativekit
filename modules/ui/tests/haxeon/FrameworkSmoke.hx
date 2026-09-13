import Color;
import Canvas;
import DisplayList;
import FontCollection;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutStyle;
import Rect;
import ResolvedLayoutItem;
import TextLayout;
import NativeKit.InputAction;
import NativeKit.TouchAction;
import NativeKit.TouchTool;
import NativeKit.TextEditAction;
import NativeKitEventValue;
import NativeKitEventValue.NativeKitTextEdit;
import NativeKitEventDecoderTests;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.State;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.UiTouchData;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TextEditorState;
import nativekit.ui.widgets.TextField;
import nativekit.ui.widgets.Utf8Text;

class FrameworkSmoke {
	static function main():Int {
		var fontPath = Sys.getEnv("NKUI_TEST_FONT_PATH");
		if (fontPath == null)
			return 2;
		var fonts = FontCollection.create();
		fonts.add(fontPath);
		var session = LayoutSession.create();
		var context = new UiContext(session, fonts);
		if (context.buildContext.fonts != fonts)
			return 30;
		var emptyEditor = new TextEditorState(fonts, "");
		if (Utf8Text.length(emptyEditor.text) != 0)
			return 38;
		emptyEditor.dispose();
		var editor = new TextEditorState(fonts, "á🙂");
		if (Utf8Text.length(editor.text) != 3)
			return 32;
		if (Utf8Text.slice(editor.text, 0, 2) != "á")
			return 34;
		if (!editor.moveCaret(-1, false) || editor.selectionFocus != 2)
			return 35;
		if (!editor.deleteForward() || editor.text != "á")
			return 36;
		if (!editor.deleteBackward() || editor.text != "")
			return 37;
		editor.insert("hi");
		var composition = new NativeKitTextEdit(TextEditAction.Compose, "á", 2, 2,
			4, 4, 2, 4);
		if (!editor.applyTextEdit(composition) || editor.text != "hiá" ||
			editor.selectionEnd != 4 || editor.compositionStart != 2 || editor.compositionEnd != 4)
			return 33;
		editor.dispose();
		var editedValue = "";
		var submittedValue = "";
		var field = new TextField("entry", "hello", function(next) { editedValue = next; },
			null, "Message");
		field.onSubmit = function(next) { submittedValue = next; };
		var fieldRoot = context.submit(field, new LayoutFrame(256.0, 192.0));
		if (fieldRoot.semantics == null || fieldRoot.semantics.role != AccessibilityRole.TextField ||
			fieldRoot.semantics.label != "Message" || !fieldRoot.focusable)
			return 40;
		var fieldState:State<TextEditorState> = context.buildContext.existingState(fieldRoot.id);
		var fieldEditor:TextEditorState = cast fieldState.value;
		if (!context.focusWidget(fieldRoot.id))
			return 41;
		context.key(UiEventKind.KeyDown, UiKey.A, UiModifier.Control);
		context.text(UiEventKind.TextInput, "á🙂");
		if (field.value != "á🙂" || editedValue != "á🙂" || fieldEditor.selectionEnd != 3)
			return 42;
		fieldRoot = context.submit(field, new LayoutFrame(256.0, 192.0));
		context.key(UiEventKind.KeyDown, UiKey.Left);
		context.key(UiEventKind.KeyDown, UiKey.Backspace);
		if (field.value != "🙂" || fieldEditor.selectionEnd != 0)
			return 43;
		var compositionEdit = new NativeKitTextEdit(TextEditAction.Compose, "x", 1, 1,
			2, 2, 1, 2);
		context.text(UiEventKind.TextEdit, null, compositionEdit);
		if (field.value != "🙂x" || fieldEditor.compositionStart != 1 ||
			fieldEditor.compositionEnd != 2)
			return 44;
		var commitEdit = new NativeKitTextEdit(TextEditAction.Commit, "x", 1, 2,
			2, 2, -1, -1);
		context.text(UiEventKind.TextEdit, null, commitEdit);
		if (fieldEditor.compositionStart != -1 || field.value != "🙂x")
			return 45;
		if (!context.accessibilityAction(fieldRoot.id.value, AccessibilityRequest.SetSelection,
			null, 0, 1, 2) || fieldEditor.selectionStart != 0 || fieldEditor.selectionEnd != 1)
			return 46;
		if (!context.accessibilityAction(fieldRoot.id.value, AccessibilityRequest.SetValue,
			"done", -1, -1, 1) || field.value != "done")
			return 47;
		fieldRoot = context.submit(field, new LayoutFrame(256.0, 192.0));
		fieldEditor = cast fieldState.value;
		if (fieldEditor.layout.selectionRects(new TextPosition(0, 0), new TextPosition(4, 0)).length == 0)
			return 48;
		var fieldTextGeometry:ResolvedLayoutItem = cast fieldRoot.children[0].resolved;
		var fieldY = fieldTextGeometry.y + fieldTextGeometry.height * 0.5;
		var fieldLeft = fieldTextGeometry.x + 0.5;
		var fieldRight = fieldTextGeometry.x + fieldTextGeometry.width - 0.5;
		context.pointerDown(fieldLeft, fieldY, 0);
		context.pointerMove(fieldRight, fieldY);
		context.pointerUp(fieldRight, fieldY, 0);
		if (fieldEditor.selectionStart != 0 || fieldEditor.selectionEnd <= 0 ||
			fieldEditor.selectionEnd > 4)
			return 49;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (submittedValue != "done")
			return 50;
		if (!NativeKitEventDecoderTests.run())
			return 27;
		var frame = new LayoutFrame(256.0, 192.0);
		var clicks = 0;
		var bubbled = 0;
		var captured = 0;
		var scrollEvents = 0;
		var keyEvents = 0;
		var repeatEvents = 0;
		var hoverEnters = 0;
		var hoverLeaves = 0;
		var cancelEvents = 0;
		var focusLostEvents = 0;
		var blurEvents = 0;
		var touchPressure = 0.0;
		var committedText = "";
		var editSeen = false;

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
				new KeyedView("next", new Button("Next")),
				new KeyedView("status", new Text("Ready", null, Color.rgba(1.0, 1.0, 1.0, 1.0)))
			], style);
		}

		var root = context.submit(makeView(true), frame);
		if (context.isDirty())
			root = context.submit(makeView(true), frame);
		root.on(UiEventKind.Click, function(_) {
			bubbled++;
		});
		root.on(UiEventKind.Click, function(_) {
			captured++;
		}, "capture");
		var buttonNode = root.children[0];
		var initialId = buttonNode.id;
		var state:State<Int> = context.buildContext.state(buttonNode.id, 0);
		if (root.children.length != 3 || buttonNode.resolved == null ||
			!buttonNode.focusable || !buttonNode.resolved.hitTest(4.0, 4.0) || context.isDirty())
			return 3;
		var semanticTree = AccessibilityBridge.project(root, buttonNode.id);
		if (semanticTree.length != 3 || semanticTree[0].id != buttonNode.id.value ||
			semanticTree[0].parentId != 0 || semanticTree[0].role != AccessibilityRole.Button ||
			(semanticTree[0].states & (AccessibilityState.Focusable | AccessibilityState.Focused)) !=
			(AccessibilityState.Focusable | AccessibilityState.Focused) ||
			(semanticTree[0].actions & AccessibilityAction.Activate) == 0 ||
			semanticTree[0].semantics.label != "Increment")
			return 25;

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

		buttonNode.on(UiEventKind.Scroll, function(_) {
			scrollEvents++;
		});
		buttonNode.on(UiEventKind.KeyDown, function(event) {
			if (event.key == UiKey.Enter && event.scancode == 28)
				keyEvents++;
		});
		buttonNode.on(UiEventKind.KeyRepeat, function(_) {
			repeatEvents++;
		});
		buttonNode.on(UiEventKind.HoverEnter, function(_) {
			hoverEnters++;
		});
		buttonNode.on(UiEventKind.HoverLeave, function(_) {
			hoverLeaves++;
		});
		buttonNode.on(UiEventKind.PointerDown, function(event) {
			if (event.pointerId != 0 && event.data != null) {
				var touchData:UiTouchData = cast event.data;
				touchPressure = touchData.pressure;
			}
		});
		buttonNode.on(UiEventKind.PointerCancel, function(_) {
			cancelEvents++;
		});
		buttonNode.on(UiEventKind.FocusLost, function(_) {
			focusLostEvents++;
		});
		buttonNode.on(UiEventKind.Blur, function(_) {
			blurEvents++;
		});
		buttonNode.on(UiEventKind.TextInput, function(event) {
			committedText = event.text;
		});
		buttonNode.on(UiEventKind.TextEdit, function(event) {
			editSeen = event.data != null && event.text == "compose";
		});
		var source:NativeKit.Handle = 17;
		var input = new NativeInputAdapter(context, source);
		var pastedText = "";
		var clipboardRequestInt = 49;
		var clipboardRequest:haxe.Int64 = clipboardRequestInt;
		context.clipboard.trackRead(clipboardRequest, function(text) {
			pastedText = text;
		});
		if (!input.consume(ClipboardText(clipboardRequest, NativeKit.Result.Ok, "from clipboard")) ||
			pastedText != "from clipboard" ||
			input.consume(ClipboardText(clipboardRequest, NativeKit.Result.Ok, "duplicate")))
			return 39;
		if (input.consume(PointerMove(18, 4.0, 4.0)))
			return 12;
		if (!input.consume(PointerMove(source, 4.0, 4.0)) || hoverEnters == 0 ||
			!input.consume(PointerScroll(source, 1.5, -24.0)) || scrollEvents != 1)
			return 13;
		if (!input.consume(PointerButton(source, 0, InputAction.Press, 0, 4.0, 4.0)) ||
			!input.consume(PointerButton(source, 0, InputAction.Release, 0, 4.0, 4.0)) || clicks != 2)
			return 14;
		if (!input.consume(Key(source, UiKey.Enter, 28, InputAction.Press, 0)) ||
			!input.consume(Key(source, UiKey.Enter, 28, InputAction.Repeat, 0)) ||
			keyEvents != 1 || repeatEvents != 1 || clicks != 3)
			return 15;
		var nextId = root.children[1].id;
		if (!input.consume(Key(source, UiKey.Tab, 15, InputAction.Press, 0)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(nextId) ||
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Press, UiModifier.Shift)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(initialId))
			return 24;
		if (!input.consume(TextInput(source, 0x1f642)) || committedText != "🙂")
			return 16;
		var edit = new NativeKitTextEdit(TextEditAction.Compose, "compose", 0, 0,
			0, 0, 0, 7);
		if (!input.consume(TextEdit(source, edit)) || !editSeen)
			return 17;
		if (!input.consume(PointerEnter(source, false)) || hoverLeaves == 0)
			return 18;
		if (!input.consume(Touch(source, 3, TouchAction.Begin, TouchTool.Finger,
			0, 4.0, 4.0, 0.75, 0.0, 0.0)) ||
			!input.consume(Touch(source, 3, TouchAction.End, TouchTool.Finger,
			0, 4.0, 4.0, 0.0, 0.0, 0.0)) || clicks != 4 || touchPressure != 0.75)
			return 19;
		input.consume(Touch(source, 4, TouchAction.Begin, TouchTool.Finger,
			0, 4.0, 4.0, 0.5, 0.0, 0.0));
		input.consume(Touch(source, 4, TouchAction.Cancel, TouchTool.Finger,
			0, 4.0, 4.0, 0.0, 0.0, 0.0));
		if (cancelEvents != 1 || clicks != 4)
			return 20;
		input.consume(WindowStateChanged(source, 0));
		if (focusLostEvents != 1 || blurEvents != 2 || context.focus.focusedId != null)
			return 21;

		context.clearFocus();
		context.focusNext();
		if (context.focus.focusedId == null || !context.focus.focusedId.equals(initialId))
			return 7;
		context.focusNext();
		if (context.focus.focusedId == null || !context.focus.focusedId.equals(nextId))
			return 22;
		context.focusPrevious();
		if (context.focus.focusedId == null || !context.focus.focusedId.equals(initialId))
			return 23;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(500.0, 500.0, 0);
		if (clicks != 4)
			return 8;
		if (!context.accessibilityAction(initialId.value, 1, null, -1, -1, 1) || clicks != 5)
			return 28;
		if (!context.accessibilityAction(initialId.value, 2, null, -1, -1, 1) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(initialId) ||
			!context.accessibilityAction(initialId.value, 3, null, -1, -1, 1) ||
			context.focus.focusedId != null)
			return 29;

		var sharedStyle = new LayoutStyle();
		new Row("style-copy", [], sharedStyle);
		if (sharedStyle.direction != LayoutDirection.TopToBottom)
			return 9;

		root = context.submit(makeView(false), frame);
		buttonNode = root.children[0];
		semanticTree = AccessibilityBridge.project(root, null);
		if ((semanticTree[0].states & AccessibilityState.Disabled) == 0)
			return 26;
		if (context.focus.focusedId != null || context.focusWidget(buttonNode.id))
			return 10;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(4.0, 4.0, 0);
		if (clicks != 5 || context.accessibilityAction(buttonNode.id.value, 1, null, -1, -1, 1))
			return 11;

		var viewportStyle = new LayoutStyle();
		viewportStyle.width = LayoutAxis.fixed(256.0);
		viewportStyle.height = LayoutAxis.fixed(80.0);
		var longContentStyle = new LayoutStyle();
		longContentStyle.width = LayoutAxis.fixed(256.0);
		longContentStyle.height = LayoutAxis.fixed(400.0);
		var scrollView = new ScrollView("demo-scroll", new Column("long-content", [
			new KeyedView("message", new Text("Scrollable content"))
		], longContentStyle), viewportStyle, ScrollAxis.Vertical);
		var scrollFrame = new LayoutFrame(256.0, 80.0);
		var scrollRoot = context.submit(scrollView, scrollFrame);
		var contentGeometry:ResolvedLayoutItem = cast scrollRoot.children[0].resolved;
		if (scrollView.controller.maxScrollY != 320.0 || contentGeometry == null ||
			contentGeometry.clipBounds.height != 80.0)
			return 25;
		context.scroll(4.0, 4.0, 0.0, -50.0);
		if (scrollView.controller.offsetY != 50.0 || !context.isDirty())
			return 26;
		scrollRoot = context.submit(scrollView, scrollFrame);
		contentGeometry = cast scrollRoot.children[0].resolved;
		if (contentGeometry.transform.ty != -50.0)
			return 27;
		scrollView.controller.jumpTo(0.0, 500.0);
		if (scrollView.controller.offsetY != 320.0 || !context.isDirty())
			return 28;

		var overlayCanvas = new Canvas();
		var overlayList = DisplayList.create();
		overlayCanvas.fillRect(new Rect(2.0, 3.0, 12.0, 8.0), Color.rgba(0.2, 0.4, 0.8, 0.5));
		overlayCanvas.update(overlayList);
		if (overlayList.info().commandCount != 2)
			return 29;
		overlayList.clear();
		overlayList.dispose();

		var cleaned = 0;
		context.stateStore.onDispose(initialId, function() {
			cleaned++;
		});
		context.dispose();
		if (cleaned != 1)
			return 31;
		fonts.dispose();
		Sys.println("PASS: Haxe framework and NativeKit pointer, touch, keyboard, and text input routing");
		return 0;
	}
}
