import Color;
import Canvas;
import DisplayList;
import FontCollection;
import Image;
import ImageFormat;
import Insets;
import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import ResolvedLayoutItem;
import Transform2D;
import TextLayout;
import TextWrap;
import NativeKit.InputAction;
import NativeKit.TouchAction;
import NativeKit.TouchTool;
import NativeKit.TextEditAction;
import NativeKitEventValue;
import NativeKitEventValue.NativeKitTextEdit;
import NativeKitEvents;
import NativeKitRuntime;
import NativeKitEventDecoderTests;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.UiTouchData;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.ImageView;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Padding;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.Popup;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.TextEditorState;
import nativekit.ui.widgets.TextEditorDiagnostics;
import nativekit.ui.widgets.TextArea;
import nativekit.ui.widgets.TextField;
import nativekit.ui.widgets.Spacer;
import nativekit.ui.widgets.Slider;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.Spinner;
import nativekit.ui.widgets.SpinnerPainter;
import nativekit.ui.widgets.SpinnerKind;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.Tooltip;
import nativekit.ui.widgets.Utf8Text;
import nativekit.ui.widgets.VirtualList;
import nativekit.ui.theme.Theme;
import nativekit.ui.gestures.GestureEvent;
import nativekit.ui.gestures.TapRecognizer;
import nativekit.ui.gestures.DoubleTapRecognizer;
import nativekit.ui.gestures.LongPressRecognizer;
import nativekit.ui.gestures.DragRecognizer;
import nativekit.ui.widgets.GestureDetector;
import nativekit.ui.widgets.RadioGroup;
import nativekit.ui.widgets.RadioOption;
import nativekit.ui.widgets.Tabs;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.animation.AnimationController;
import nativekit.ui.animation.SpringController;
import nativekit.ui.animation.LoopAnimation;
import nativekit.ui.animation.Easing;
import nativekit.ui.debug.UiInspector;
import nativekit.ui.debug.AccessibilityAudit;
import AccessibilityContract;

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
		var blinkEditor = new TextEditorState(fonts, "caret");
		blinkEditor.focused = true;
		blinkEditor.resetCaretBlink(10.0);
		if (!blinkEditor.isCaretVisible(10.49) || blinkEditor.isCaretVisible(10.5) ||
			!blinkEditor.isCaretVisible(11.0))
			return 225;
		blinkEditor.focused = false;
		if (blinkEditor.isCaretVisible(11.1))
			return 226;
		blinkEditor.dispose();
		var editedValue = "";
		var submittedValue = "";
		var emptyField = new TextField("empty-entry", "", function(_) {}, null,
			"Empty message");
		var emptyRoot = context.submit(emptyField, new LayoutFrame(256.0, 192.0));
		if (emptyRoot.semantics == null || emptyRoot.semantics.value != "")
			return 39;
		var field = new TextField("entry", "hello", function(next) { editedValue = next; },
			null, "Message");
		field.onSubmit = function(next) { submittedValue = next; };
		var fieldDiagnostics:Null<TextEditorDiagnostics> = null;
		field.onDiagnostics = function(next) { fieldDiagnostics = next; };
		var fieldRoot = context.submit(field, new LayoutFrame(256.0, 192.0));
		if (fieldRoot.children.length != 1 ||
			fieldRoot.children[0].layout.visualKind != LayoutVisualKind.Box ||
			fieldRoot.children[0].children.length != 3 ||
			fieldRoot.children[0].children[0].layout.visualKind != LayoutVisualKind.Custom ||
			fieldRoot.children[0].children[0].layout.style.zIndex != 0 ||
			fieldRoot.children[0].children[1].layout.visualKind != LayoutVisualKind.Text ||
			fieldRoot.children[0].children[1].layout.style.zIndex != 1 ||
			fieldRoot.children[0].children[2].layout.visualKind != LayoutVisualKind.Custom ||
			fieldRoot.children[0].children[2].layout.style.zIndex != 2)
			return 201;
		if (fieldRoot.semantics == null || fieldRoot.semantics.role != AccessibilityRole.TextField ||
			fieldRoot.semantics.label != "Message" || !fieldRoot.focusable)
			return 40;
		var fieldState:State<TextEditorState> = context.buildContext.existingState(fieldRoot.id);
		var fieldEditor:TextEditorState = cast fieldState.value;
		if (!context.focusWidget(fieldRoot.id))
			return 41;
		if (fieldDiagnostics == null || !fieldDiagnostics.focused ||
			fieldDiagnostics.selectionStart != 5 || fieldDiagnostics.selectionEnd != 5 ||
			fieldDiagnostics.caretOffset != 5 || fieldDiagnostics.caretRect == null)
			return 209;
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
		fieldRoot = context.submit(field, new LayoutFrame(256.0, 192.0));
		if (fieldDiagnostics == null || !fieldDiagnostics.focused ||
			fieldDiagnostics.compositionStart != 1 || fieldDiagnostics.compositionEnd != 2 ||
			fieldDiagnostics.compositionText != "x" || fieldDiagnostics.caretRect == null)
			return 213;
		var commitEdit = new NativeKitTextEdit(TextEditAction.Commit, "x", 1, 2,
			2, 2, -1, -1);
		context.text(UiEventKind.TextEdit, null, commitEdit);
		if (fieldEditor.compositionStart != -1 || field.value != "🙂x")
			return 45;
		var cancelEdit = new NativeKitTextEdit(TextEditAction.SetComposition, null, 0, 0,
			2, 2, 1, 2);
		if (!fieldEditor.applyTextEdit(cancelEdit) || fieldEditor.compositionStart != 1 ||
			fieldEditor.compositionRects().length == 0 ||
			!fieldEditor.applyTextEdit(new NativeKitTextEdit(TextEditAction.FinishComposition,
				null, 0, 0, 2, 2, -1, -1)) || fieldEditor.compositionStart != -1)
			return 210;
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
		var fieldTextGeometry:ResolvedLayoutItem =
			cast fieldRoot.children[0].children[1].resolved;
		var fieldY = fieldTextGeometry.y + fieldTextGeometry.height * 0.5;
		var fieldLeft = fieldTextGeometry.x + 0.5;
		var fieldRight = fieldTextGeometry.x + fieldTextGeometry.width - 0.5;
		context.pointerDown(fieldLeft, fieldY, 0);
		context.pointerMove(fieldRight, fieldY);
		context.pointerUp(fieldRight, fieldY, 0);
		if (fieldEditor.selectionStart != 0 || fieldEditor.selectionEnd <= 0 ||
			fieldEditor.selectionEnd > 4)
			return 49;
		var endCaret = fieldEditor.layout.caret(new TextPosition(4, 0));
		var endX = fieldTextGeometry.x + endCaret.x + 3.0;
		context.pointerDown(endX, fieldY, 0);
		context.pointerUp(endX, fieldY, 0);
		if (fieldEditor.selectionStart != 4 || fieldEditor.selectionEnd != 4 ||
			fieldEditor.selectionFocus != 4)
			return 98;
		var placedCaret = fieldEditor.layout.caret(fieldEditor.focusPosition());
		var caretDelta = placedCaret.x - endCaret.x;
		if (caretDelta < -0.1 || caretDelta > 0.1)
			return 104;
		var clickFrame = new LayoutFrame(256.0, 192.0);
		clickFrame.deltaSeconds = 0.1;
		fieldRoot = context.submit(field, clickFrame);
		fieldTextGeometry = cast fieldRoot.children[0].children[1].resolved;
		var wordX = fieldTextGeometry.x + fieldEditor.layout.measure().width * 0.5;
		context.pointerDown(wordX, fieldY, 0);
		context.pointerUp(wordX, fieldY, 0);
		clickFrame = new LayoutFrame(256.0, 192.0);
		clickFrame.deltaSeconds = 0.1;
		fieldRoot = context.submit(field, clickFrame);
		context.pointerDown(wordX, fieldY, 0);
		context.pointerUp(wordX, fieldY, 0);
		if (fieldEditor.selectionStart != 0 || fieldEditor.selectionEnd != 4)
			return 99;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (submittedValue != "done")
			return 50;

		var arabicField = new TextField("multi-arabic", "مرحبا بالعالم", null, null, "Arabic");
		var hebrewField = new TextField("multi-hebrew", "שלום עולם", null, null, "Hebrew");
		var multiFields = new Column("multi-fields", [
			new KeyedView("arabic", arabicField),
			new KeyedView("hebrew", hebrewField)
		]);
		var multiRoot = context.submit(multiFields, new LayoutFrame(320.0, 240.0));
		var arabicRoot = multiRoot.children[0];
		var hebrewRoot = multiRoot.children[1];
		var arabicState:State<TextEditorState> = context.buildContext.existingState(arabicRoot.id);
		var hebrewState:State<TextEditorState> = context.buildContext.existingState(hebrewRoot.id);
		var arabicEditor:TextEditorState = cast arabicState.value;
		var hebrewEditor:TextEditorState = cast hebrewState.value;
		var arabicLength = Utf8Text.length(arabicEditor.text);
		var hebrewLength = Utf8Text.length(hebrewEditor.text);
		arabicEditor.setSelection(0, arabicLength);
		hebrewEditor.setSelection(0, hebrewLength);
		if (arabicEditor.layout.selectionRects(new TextPosition(0, 0),
			new TextPosition(arabicLength, 0)).length == 0 ||
			hebrewEditor.layout.selectionRects(new TextPosition(0, 0),
				new TextPosition(hebrewLength, 0)).length == 0)
			return 220;
		if (!context.focusWidget(arabicRoot.id) || !context.textInput.isOwner(arabicRoot.id) ||
			!arabicEditor.focused || hebrewEditor.focused)
			return 221;
		if (!context.focusWidget(hebrewRoot.id) || !context.textInput.isOwner(hebrewRoot.id) ||
			arabicEditor.focused || !hebrewEditor.focused ||
			arabicEditor.selectionStart != 0 || arabicEditor.selectionEnd != arabicLength ||
			hebrewEditor.selectionStart != 0 || hebrewEditor.selectionEnd != hebrewLength)
			return 222;
		context.textInput.deactivate(arabicRoot.id);
		if (!context.textInput.isOwner(hebrewRoot.id))
			return 223;
		context.text(UiEventKind.TextInput, "x");
		if (arabicEditor.text != "مرحبا بالعالم" || hebrewEditor.text != "x")
			return 224;

		var wordArea = new TextArea("word-navigation", "one two\nthree four");
		var wordAreaRoot = context.submit(wordArea, new LayoutFrame(256.0, 192.0));
		var wordAreaState:State<TextEditorState> = context.buildContext.existingState(wordAreaRoot.id);
		var wordEditor:TextEditorState = cast wordAreaState.value;
		if (!context.focusWidget(wordAreaRoot.id))
			return 190;
		var wordRange = wordEditor.layout.wordRangeAt(5);
		if (wordRange.start != 4 || wordRange.end != 7)
			return 191;
		var wordTextGeometry:ResolvedLayoutItem =
			cast wordAreaRoot.children[0].children[1].resolved;
		var wordStartCaret = wordEditor.layout.caret(new TextPosition(4, 0));
		var wordStartX = wordStartCaret.x;
		var wordNextX = wordEditor.layout.caret(new TextPosition(5, 0)).x;
		var wordClickX = wordTextGeometry.x + (wordStartX + wordNextX) * 0.5;
		var wordClickY = wordTextGeometry.y + wordStartCaret.y +
			(wordStartCaret.ascender + wordStartCaret.descender) * 0.5;
		context.pointerDown(wordClickX, wordClickY, 0, 0, 0, null, 1.0);
		if (wordEditor.selectionStart != wordEditor.selectionEnd ||
			wordEditor.selectionFocus < 4 || wordEditor.selectionFocus > 5)
			return 202;
		context.pointerUp(wordClickX, wordClickY, 0);
		context.pointerDown(wordClickX, wordClickY, 0, 0, 0, null, 1.1);
		if (wordEditor.selectionStart != 4 || wordEditor.selectionEnd != 7)
			return 192;
		if (wordEditor.layout.selectionRects(new TextPosition(4, 0),
			new TextPosition(7, 0)).length == 0)
			return 200;
		context.pointerUp(wordClickX, wordClickY, 0);
		context.pointerDown(wordClickX, wordClickY, 0, 0, 0, null, 1.2);
		if (wordEditor.selectionStart != 0 || wordEditor.selectionEnd != 8)
			return 193;
		context.pointerUp(wordClickX, wordClickY, 0);
		#if (mac || ios)
		wordEditor.placeCaret(4, false);
		context.key(UiEventKind.KeyDown, UiKey.Right, UiModifier.Alt);
		if (wordEditor.selectionFocus != 7)
			return 196;
		wordEditor.placeCaret(4, false);
		context.key(UiEventKind.KeyDown, UiKey.Right, UiModifier.Alt | UiModifier.Shift);
		if (wordEditor.selectionFocus != 7 || wordEditor.selectionStart != 4 ||
			wordEditor.selectionEnd != 7)
			return 195;
		wordEditor.placeCaret(7, false);
		context.key(UiEventKind.KeyDown, UiKey.Left, UiModifier.Alt | UiModifier.Shift);
		if (wordEditor.selectionFocus != 4 || wordEditor.selectionStart != 4 ||
			wordEditor.selectionEnd != 7)
			return 199;
		wordEditor.placeCaret(10, false);
		context.key(UiEventKind.KeyDown, UiKey.Up, UiModifier.Alt);
		if (wordEditor.selectionFocus != 8)
			return 197;
		wordEditor.placeCaret(1, false);
		context.key(UiEventKind.KeyDown, UiKey.Down, UiModifier.Alt);
		if (wordEditor.selectionFocus != 7)
			return 198;
		#else
		wordEditor.placeCaret(0, false);
		context.key(UiEventKind.KeyDown, UiKey.Right, UiModifier.Control);
		if (wordEditor.selectionFocus != 4 || wordEditor.selectionStart != 4 ||
			wordEditor.selectionEnd != 4)
			return 194;
		context.key(UiEventKind.KeyDown, UiKey.Right,
			UiModifier.Control | UiModifier.Shift);
		if (wordEditor.selectionFocus != 7 || wordEditor.selectionStart != 4 ||
			wordEditor.selectionEnd != 7)
			return 195;
		wordEditor.placeCaret(7, false);
		context.key(UiEventKind.KeyDown, UiKey.Left,
			UiModifier.Control | UiModifier.Shift);
		if (wordEditor.selectionFocus != 4 || wordEditor.selectionStart != 4 ||
			wordEditor.selectionEnd != 7)
			return 199;
		wordEditor.placeCaret(8, false);
		context.key(UiEventKind.KeyDown, UiKey.Up, UiModifier.Control);
		if (wordEditor.selectionFocus != 0)
			return 197;
		wordEditor.placeCaret(0, false);
		context.key(UiEventKind.KeyDown, UiKey.Down, UiModifier.Control);
		if (wordEditor.selectionFocus != 8)
			return 198;
		#end
		var lineArea = new TextArea("visual-line-navigation", "first line\nsecond\nthird line");
		var lineRoot = context.submit(lineArea, new LayoutFrame(256.0, 192.0));
		var lineState:State<TextEditorState> = context.buildContext.existingState(lineRoot.id);
		var lineEditor:TextEditorState = cast lineState.value;
		var lineTextGeometry:ResolvedLayoutItem =
			cast lineRoot.children[0].children[1].resolved;
		lineEditor.updateLayout(lineTextGeometry.width);
		lineEditor.placeCaret(15, false);
		if (!lineEditor.moveCaretToLineBoundary(false, false) || lineEditor.selectionFocus != 11)
			return 203;
		lineEditor.placeCaret(15, false);
		if (!lineEditor.moveCaretToLineBoundary(true, false) || lineEditor.selectionFocus != 17)
			return 204;
		lineEditor.placeCaret(15, false);
		if (!lineEditor.moveCaretVertically(-1, false) ||
			lineEditor.layout.lineRangeAt(lineEditor.selectionFocus).start != 0)
			return 205;
		if (!lineEditor.moveCaretVertically(1, false) ||
			lineEditor.layout.lineRangeAt(lineEditor.selectionFocus).start != 11)
			return 206;
		lineEditor.replace(0, Utf8Text.length(lineEditor.text),
			"one\ntwo\nthree\nfour\nfive\nsix\nseven\neight");
		lineEditor.updateLayout(lineTextGeometry.width);
		lineEditor.placeCaret(Utf8Text.length(lineEditor.text), false);
		if (!lineEditor.ensureCaretVisible(30.0) || lineEditor.scrollOffsetY <= 0.0)
			return 207;
		lineEditor.placeCaret(0, false);
		if (!lineEditor.ensureCaretVisible(30.0) || lineEditor.scrollOffsetY != 0.0)
			return 208;
		lineEditor.setSelection(0, Utf8Text.length(lineEditor.text));
		if (!lineEditor.ensureCaretVisible(30.0) || lineEditor.scrollOffsetY <= 0.0)
			return 211;
		lineEditor.placeCaret(0, false);
		if (!lineEditor.ensureCaretVisible(30.0) || lineEditor.scrollOffsetY != 0.0)
			return 212;
		var keyboardArea = new TextArea("keyboard-line-navigation",
			"one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve");
		var keyboardRoot = context.submit(keyboardArea, new LayoutFrame(256.0, 192.0));
		var keyboardState:State<TextEditorState> = context.buildContext.existingState(keyboardRoot.id);
		var keyboardEditor:TextEditorState = cast keyboardState.value;
		if (!context.focusWidget(keyboardRoot.id))
			return 214;
		keyboardEditor.placeCaret(0, false);
		context.key(UiEventKind.KeyDown, UiKey.End);
		if (keyboardEditor.selectionFocus != 3)
			return 215;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		context.key(UiEventKind.KeyDown, UiKey.Home);
		if (keyboardEditor.selectionFocus != 4)
			return 216;
		keyboardEditor.placeCaret(0, false);
		for (_ in 0...12)
			context.key(UiEventKind.KeyDown, UiKey.Down);
		if (keyboardEditor.scrollOffsetY <= 0.0)
			return 217;
		context.key(UiEventKind.KeyDown, UiKey.Home, UiModifier.Control);
		if (keyboardEditor.selectionFocus != 0 || keyboardEditor.scrollOffsetY != 0.0)
			return 218;
		var clickField = new TextField("single-double-click", "first last", null,
			null, "Click selection");
		var clickFieldRoot = context.submit(clickField, new LayoutFrame(256.0, 192.0));
		var clickFieldState:State<TextEditorState> = context.buildContext.existingState(clickFieldRoot.id);
		var clickEditor:TextEditorState = cast clickFieldState.value;
		var clickTextGeometry:ResolvedLayoutItem = cast clickFieldRoot.children[0].resolved;
		var clickY = clickTextGeometry.y + clickTextGeometry.height * 0.5;
		var clickX = clickTextGeometry.x + clickTextGeometry.width - 2.0;
		var clickPosition = clickEditor.hitTest(clickX - clickTextGeometry.x,
			clickY - clickTextGeometry.y);
		var clickOffset = clickEditor.layout.offsetFromPosition(clickPosition);
		if (clickOffset != 10)
			return 105;
		context.pointerDown(clickX, clickY, 0);
		context.pointerUp(clickX, clickY, 0);
		if (clickEditor.selectionStart != 10 || clickEditor.selectionEnd != 10)
			return 106;
		var doubleClickFrame = new LayoutFrame(256.0, 192.0);
		doubleClickFrame.deltaSeconds = 0.1;
		clickFieldRoot = context.submit(clickField, doubleClickFrame);
		context.pointerDown(clickX, clickY, 0);
		context.pointerUp(clickX, clickY, 0);
		if (clickEditor.selectionStart != 6 || clickEditor.selectionEnd != 10)
			return 107;
		var areaChanged = "";
		var area = new TextArea("area-smoke", "line", function(next) { areaChanged = next; });
		var areaRoot = context.submit(area, new LayoutFrame(256.0, 192.0));
		var areaSemantics:Semantics = cast areaRoot.semantics;
		var areaGeometry:ResolvedLayoutItem = cast areaRoot.resolved;
		if ((areaSemantics.states & AccessibilityState.Multiline) == 0 || areaGeometry.height < 100.0 ||
			!context.focusWidget(areaRoot.id))
			return 62;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (area.value != "line\n" || areaChanged != "line\n")
			return 63;
		var alignStyle = new LayoutStyle();
		alignStyle.width = LayoutAxis.fixed(160.0);
		alignStyle.height = LayoutAxis.fixed(80.0);
		var alignRoot = context.submit(new Align("align-smoke", new Text("Centered"),
			LayoutAlignmentX.Center, LayoutAlignmentY.Center, alignStyle),
			new LayoutFrame(256.0, 192.0));
		var alignParentGeometry:ResolvedLayoutItem = cast alignRoot.resolved;
		var alignChildGeometry:ResolvedLayoutItem = cast alignRoot.children[0].resolved;
		if (alignChildGeometry.x < alignParentGeometry.x +
			(alignParentGeometry.width - alignChildGeometry.width) * 0.5 - 0.1 ||
			alignChildGeometry.x > alignParentGeometry.x +
			(alignParentGeometry.width - alignChildGeometry.width) * 0.5 + 0.1 ||
			alignChildGeometry.y < alignParentGeometry.y +
			(alignParentGeometry.height - alignChildGeometry.height) * 0.5 - 0.1 ||
			alignChildGeometry.y > alignParentGeometry.y +
			(alignParentGeometry.height - alignChildGeometry.height) * 0.5 + 0.1)
			return 51;
		var paddingStyle = new LayoutStyle();
		paddingStyle.width = LayoutAxis.fixed(100.0);
		paddingStyle.height = LayoutAxis.fixed(60.0);
		var paddingRoot = context.submit(new Padding("padding-smoke", new Text("Inset"),
			new Insets(7.0, 9.0, 11.0, 13.0), paddingStyle),
			new LayoutFrame(256.0, 192.0));
		var paddingParentGeometry:ResolvedLayoutItem = cast paddingRoot.resolved;
		var paddingChildGeometry:ResolvedLayoutItem = cast paddingRoot.children[0].resolved;
		if (paddingChildGeometry.x != paddingParentGeometry.x + 7.0 ||
			paddingChildGeometry.y != paddingParentGeometry.y + 9.0)
			return 52;
		var spacerRowStyle = new LayoutStyle();
		spacerRowStyle.width = LayoutAxis.fixed(200.0);
		spacerRowStyle.height = LayoutAxis.fixed(36.0);
		var spacerRow = new Row("spacer-row", [
			new KeyedView("left", new Text("L")),
			new KeyedView("gap", new Spacer("gap", LayoutAxis.grow(), LayoutAxis.fit())),
			new KeyedView("right", new Text("R"))
		], spacerRowStyle);
		var spacerRoot = context.submit(spacerRow, new LayoutFrame(256.0, 192.0));
		var spacerGeometry:ResolvedLayoutItem = cast spacerRoot.children[1].resolved;
		var leftTextGeometry:ResolvedLayoutItem = cast spacerRoot.children[0].resolved;
		var rightTextGeometry:ResolvedLayoutItem = cast spacerRoot.children[2].resolved;
		if (spacerGeometry.width <= 0.0 || rightTextGeometry.x <= leftTextGeometry.x)
			return 53;
		var lowerLayerClicks = 0;
		var upperLayerClicks = 0;
		var stack = new Stack("stack-smoke", [
			new StackChild("lower", new Button("Lower", null,
				function() { lowerLayerClicks++; }), 20.0, 20.0, 1),
			new StackChild("upper", new Button("Upper", null,
				function() { upperLayerClicks++; }), 20.0, 20.0, 5)
		]);
		var stackRoot = context.submit(stack, new LayoutFrame(256.0, 192.0));
		var lowerGeometry:ResolvedLayoutItem = cast stackRoot.children[0].resolved;
		var upperGeometry:ResolvedLayoutItem = cast stackRoot.children[1].resolved;
		if (lowerGeometry.x != upperGeometry.x || lowerGeometry.y != upperGeometry.y)
			return 64;
		var stackX = upperGeometry.x + 1.0;
		var stackY = upperGeometry.y + 1.0;
		context.pointerDown(stackX, stackY, 0);
		context.pointerUp(stackX, stackY, 0);
		if (lowerLayerClicks != 0 || upperLayerClicks != 1)
			return 65;
		var checkboxChanged = false;
		var checkbox = new Checkbox("check-smoke", "Remember", false,
			function(next) { checkboxChanged = next; });
		var checkboxRoot = context.submit(checkbox, new LayoutFrame(256.0, 192.0));
		var checkboxSemantics:Semantics = cast checkboxRoot.semantics;
		if (checkboxSemantics == null || checkboxSemantics.role != AccessibilityRole.Checkbox ||
			(checkboxSemantics.states & AccessibilityState.Checked) != 0 ||
			!context.focusWidget(checkboxRoot.id))
			return 54;
		context.key(UiEventKind.KeyDown, UiKey.Space);
		if (!checkbox.checked || !checkboxChanged ||
			(checkboxSemantics.states & AccessibilityState.Checked) == 0)
			return 55;
		var narrowButtonStyle = new LayoutStyle();
		narrowButtonStyle.width = LayoutAxis.fixed(88.0);
		narrowButtonStyle.height = LayoutAxis.fixed(38.0);
		var narrowButtonRoot = context.submit(new Button("Selected", narrowButtonStyle),
			new LayoutFrame(256.0, 192.0));
		if (narrowButtonRoot.children[0].layout.paragraphStyle.wrap != TextWrap.None)
			return 102;
		var toggleChanged = false;
		var toggle = new Toggle("toggle-smoke", "Enabled", false,
			function(next) { toggleChanged = next; });
		var toggleRoot = context.submit(toggle, new LayoutFrame(256.0, 192.0));
		var toggleSemantics:Semantics = cast toggleRoot.semantics;
		var toggleGeometry:ResolvedLayoutItem = cast toggleRoot.resolved;
		context.pointerDown(toggleGeometry.x + toggleGeometry.width * 0.5,
			toggleGeometry.y + toggleGeometry.height * 0.5, 0);
		context.pointerUp(toggleGeometry.x + toggleGeometry.width * 0.5,
			toggleGeometry.y + toggleGeometry.height * 0.5, 0);
		if (!toggle.checked || !toggleChanged || toggleSemantics.role != AccessibilityRole.Switch ||
			(toggleSemantics.actions & AccessibilityAction.Toggle) == 0)
			return 56;
		var sliderChanged = 0.0;
		var slider = new Slider("slider-smoke", "Level", 0.5, 0.0, 1.0, 0.1,
			function(next) { sliderChanged = next; });
		var sliderRoot = context.submit(slider, new LayoutFrame(256.0, 192.0));
		var sliderSemantics:Semantics = cast sliderRoot.semantics;
		if (sliderSemantics == null || sliderSemantics.role != AccessibilityRole.Slider ||
			!context.focusWidget(sliderRoot.id))
			return 57;
		context.key(UiEventKind.KeyDown, UiKey.Right);
		if (slider.value < 0.59 || slider.value > 0.61 || sliderChanged != slider.value)
			return 58;
		if (!context.accessibilityAction(sliderRoot.id.value, AccessibilityRequest.SetValue,
			"0.8", -1, -1, 1) || slider.value < 0.79 || slider.value > 0.81)
			return 59;
		sliderRoot = context.submit(slider, new LayoutFrame(256.0, 192.0));
		var sliderGeometry:ResolvedLayoutItem = cast sliderRoot.resolved;
		var sliderY = sliderGeometry.y + sliderGeometry.height * 0.5;
		var pointerValue = 0.25;
		var sliderX = sliderGeometry.x + 10.0 + (sliderGeometry.width - 20.0) * pointerValue;
		context.pointerDown(sliderX, sliderY, 0);
		context.pointerUp(sliderX, sliderY, 0);
		if (slider.value < 0.29 || slider.value > 0.31)
			return 60;
		sliderX = sliderGeometry.x + 10.0 + (sliderGeometry.width - 20.0) * 0.25;
		context.pointerDown(sliderX, sliderY, 0);
		sliderRoot = context.submit(slider, new LayoutFrame(256.0, 192.0));
		sliderGeometry = cast sliderRoot.resolved;
		var dragX = sliderGeometry.x + 10.0 + (sliderGeometry.width - 20.0) * 0.85;
		context.pointerMove(dragX, sliderY);
		if (slider.value < 0.89 || slider.value > 0.91)
			return 100;
		context.pointerUp(dragX, sliderY, 0);
		var progressRoot = context.submit(new ProgressBar("progress-smoke", 0.75,
			0.0, 1.0, "Transfer"), new LayoutFrame(256.0, 192.0));
		var progressSemantics:Semantics = cast progressRoot.semantics;
		if (progressSemantics == null || progressSemantics.role != AccessibilityRole.ProgressBar ||
			(progressSemantics.states & AccessibilityState.ReadOnly) == 0 ||
			progressSemantics.numericValue != 0.75)
			return 61;
		var imageBytes = haxe.io.Bytes.alloc(16);
		for (index in 0...16)
			imageBytes.set(index, 255);
		var image = Image.create(2, 2, ImageFormat.RGBA8, imageBytes);
		var imageRoot = context.submit(new ImageView("image-smoke", image, "Picture"),
			new LayoutFrame(256.0, 192.0));
		var imageSemantics:Semantics = cast imageRoot.semantics;
		var imageGeometry:ResolvedLayoutItem = cast imageRoot.resolved;
		if (imageSemantics.role != AccessibilityRole.Image || imageSemantics.label != "Picture" ||
			imageGeometry.width != 2.0 || imageGeometry.height != 2.0)
			return 64;
		var imageCanvas = new Canvas();
		var imageList = DisplayList.create();
		imageCanvas.drawImage(image, new Rect(1.0, 2.0, 20.0, 12.0));
		imageCanvas.update(imageList);
		if (imageList.info().commandCount != 1)
			return 65;
		imageList.clear();
		imageList.dispose();
		image.dispose();
		var canvasEvents = 0;
		var canvasView = new CanvasView("canvas-smoke", function(canvas, geometry) {
			canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
				Color.rgba(0.3, 0.4, 0.5, 1.0));
		}, null, "Drawing region");
		canvasView.on(UiEventKind.PointerDown, function(_) { canvasEvents++; });
		var canvasRoot = context.submit(canvasView, new LayoutFrame(256.0, 192.0));
		var canvasGeometry:ResolvedLayoutItem = cast canvasRoot.resolved;
		var canvasSemantics:Semantics = cast canvasRoot.semantics;
		context.pointerDown(canvasGeometry.x + 10.0, canvasGeometry.y + 10.0, 0);
		if (canvasEvents != 1 || canvasSemantics.role != AccessibilityRole.Group)
			return 66;
		if (!NativeKitEventDecoderTests.run())
			return 27;
		var frame = new LayoutFrame(256.0, 192.0);
		var cycleRoot = new RenderNode(new WidgetId(0x7fffff00));
		var cycleChild = new RenderNode(new WidgetId(0x7fffff01));
		cycleRoot.children.push(cycleChild);
		cycleChild.children.push(cycleRoot);
		if (cycleRoot.find(new WidgetId(0x7fffff02)) != null)
			return 105;
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
		var source = new NativeKit.Handle(17);
		var input = new NativeInputAdapter(context, source);
		var eventRuntime = NativeKitRuntime.start();
		var eventPump = eventRuntime.events;
		var pumpEvents = 0;
		var pumpSubscription = eventPump.listen(function(_) { pumpEvents++; });
		input.attach(eventPump);
		input.attach(eventPump);
		eventPump.dispatch(PointerMove(source, 4.0, 4.0));
		if (pumpEvents != 1 || hoverEnters != 1)
			return 101;
		input.detach();
		eventPump.dispatch(PointerEnter(source, false));
		if (pumpEvents != 2 || hoverLeaves != 0)
			return 102;
		pumpSubscription.dispose();
		eventPump.dispatch(PointerMove(source, 5.0, 5.0));
		if (pumpEvents != 2 || !pumpSubscription.isDisposed())
			return 103;
		eventRuntime.dispose();
		if (!eventPump.isDisposed())
			return 104;
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
		if (input.consume(PointerMove(new NativeKit.Handle(18), 4.0, 4.0)))
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
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Repeat, 0)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(initialId) ||
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Repeat, 0)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(nextId) ||
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Press, UiModifier.Shift)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(initialId) ||
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Repeat, UiModifier.Shift)) ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(nextId) ||
			!input.consume(Key(source, UiKey.Tab, 15, InputAction.Repeat, UiModifier.Shift)) ||
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
		if (focusLostEvents != 1 || blurEvents != 4 || context.focus.focusedId != null)
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
		var builtRows:Array<Int> = [];
		var listController = new ScrollController();
		var virtualStyle = new LayoutStyle();
		virtualStyle.width = LayoutAxis.fixed(256.0);
		virtualStyle.height = LayoutAxis.fixed(80.0);
		var virtualList = new VirtualList("virtual-smoke", 100, 20.0, function(index) {
			builtRows.push(index);
			return new Text('Row $index');
		}, virtualStyle, null, listController, 80.0);
		var virtualRoot = context.submit(virtualList, new LayoutFrame(256.0, 80.0));
		var virtualSemantics:Semantics = cast virtualRoot.semantics;
		if (virtualSemantics.role != AccessibilityRole.Collection || virtualSemantics.setSize != 100 ||
			builtRows.length >= 12 ||
			listController.maxScrollY != 1920.0)
			return 67;
		listController.jumpTo(0.0, 500.0);
		builtRows.resize(0);
		virtualRoot = context.submit(virtualList, new LayoutFrame(256.0, 80.0));
		if (builtRows.length >= 12 || builtRows.length == 0 || builtRows[0] < 24 ||
			builtRows[0] > 25 || listController.offsetY != 500.0)
			return 68;

		// Exercise the session capacity and the framework as one realistic,
		// nested settings tree. The custom painter sits between ordinary text
		// siblings inside the clipped, scrollable content.
		var pressureCanvasStyle = new LayoutStyle();
		pressureCanvasStyle.width = LayoutAxis.grow();
		pressureCanvasStyle.height = LayoutAxis.fixed(24.0);
		var pressureChildren:Array<KeyedView> = [
			new KeyedView("before-custom", new Text("Before custom canvas")),
			new KeyedView("custom-canvas", new CanvasView("custom-canvas-view",
				function(canvas, geometry) {
					canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
						Color.rgba(0.2, 0.4, 0.7, 1.0));
				}, pressureCanvasStyle, "Custom settings illustration")),
			new KeyedView("after-custom", new Text("After custom canvas"))
		];
		for (index in 0...2000) {
			var row = new SizedBox("row", new Text('Setting $index'), LayoutAxis.grow(),
				LayoutAxis.fixed(24.0));
			pressureChildren.push(new KeyedView('setting:$index', row));
		}
		var pressureContentStyle = new LayoutStyle();
		pressureContentStyle.width = LayoutAxis.grow();
		var pressureContent = new Column("pressure-settings", pressureChildren,
			pressureContentStyle);
		var innerViewportStyle = new LayoutStyle();
		innerViewportStyle.width = LayoutAxis.fixed(256.0);
		innerViewportStyle.height = LayoutAxis.fixed(128.0);
		var innerController = new ScrollController();
		var innerScroll = new ScrollView("pressure-inner-scroll", pressureContent,
			innerViewportStyle, ScrollAxis.Vertical, innerController);
		var outerContentStyle = new LayoutStyle();
		outerContentStyle.width = LayoutAxis.fixed(256.0);
		outerContentStyle.height = LayoutAxis.fit();
		var outerContent = new Column("pressure-outer-content", [
			new KeyedView("pressure-heading", new Text("Settings")),
			new KeyedView("pressure-inner", innerScroll),
			new KeyedView("pressure-tail", new SizedBox("tail", new Text("End of section"),
				LayoutAxis.grow(), LayoutAxis.fixed(500.0)))
		], outerContentStyle);
		var outerViewportStyle = new LayoutStyle();
		outerViewportStyle.width = LayoutAxis.fixed(256.0);
		outerViewportStyle.height = LayoutAxis.fixed(192.0);
		var outerController = new ScrollController();
		var pressureView = new ScrollView("pressure-outer-scroll", outerContent,
			outerViewportStyle, ScrollAxis.Vertical, outerController);
		var pressureRoot = context.submit(pressureView, new LayoutFrame(256.0, 192.0));
		var pressureNodeCount = 0;
		pressureRoot.walk(function(_) { pressureNodeCount++; });
		var innerNode = pressureRoot.children[0].children[0].children[1];
		var innerGeometry:ResolvedLayoutItem = cast innerNode.resolved;
		var customNode = innerNode.children[0].children[0].children[1];
		var customParent:RenderNode = cast customNode.parent;
		if (pressureNodeCount < 4000 || innerGeometry.height != 128.0 ||
			innerController.maxScrollY < 47000.0 || outerController.maxScrollY < 300.0 ||
			customParent == null || customNode.layout.visualKind != LayoutVisualKind.Custom ||
			customParent.children[0].layout.visualKind != LayoutVisualKind.Text ||
			customParent.children[2].layout.visualKind != LayoutVisualKind.Text)
			return 101;
		context.scroll(innerGeometry.x + 4.0, innerGeometry.y + 4.0, 0.0, -60.0);
		if (innerController.offsetY != 60.0 || outerController.offsetY != 0.0)
			return 102;
		context.scroll(4.0, 180.0, 0.0, -50.0);
		if (outerController.offsetY != 50.0)
			return 103;

		var returnFocusView = new Button("Return focus");
		var returnFocusRoot = context.submit(returnFocusView, new LayoutFrame(256.0, 192.0));
		if (!context.focusWidget(returnFocusRoot.id))
			return 69;
		var dialogDismissals = 0;
		var dialog = new Dialog("dialog-smoke", "Settings", new Button("Apply"),
			function() { dialogDismissals++; }, 240.0);
		var dialogFrame = new LayoutFrame(256.0, 192.0);
		var modalStack = new Stack("modal-stack", [
			new StackChild("background", new Text("Background"), 0.0, 0.0, 0,
				LayoutAxis.grow(), LayoutAxis.grow()),
			new StackChild("dialog-layer", dialog, 0.0, 0.0, 10,
				LayoutAxis.grow(), LayoutAxis.grow())
		]);
		var dialogRoot = context.submit(modalStack, dialogFrame);
		var dialogFocus:Null<WidgetId> = null;
		dialogRoot.walk(function(node) {
			if (node.focusable)
				dialogFocus = node.id;
		});
		var dialogLayer = dialogRoot.children[1];
		var dialogPanelGeometry:ResolvedLayoutItem = cast dialogLayer.children[1].children[0].resolved;
		var centeredDialogX = 128.0 - dialogPanelGeometry.width * 0.5;
		if (dialogFocus == null || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(dialogFocus) ||
			dialogPanelGeometry.x < centeredDialogX - 0.1 ||
			dialogPanelGeometry.x > centeredDialogX + 0.1)
			return 70;
		var modalSemantics = AccessibilityBridge.project(dialogRoot, dialogFocus);
		for (semanticNode in modalSemantics)
			if (semanticNode.semantics.label == "Background")
				return 79;
		context.key(UiEventKind.KeyDown, UiKey.Escape);
		if (dialogDismissals != 1)
			return 71;
		returnFocusRoot = context.submit(returnFocusView, dialogFrame);
		if (context.focus.focusedId == null || !context.focus.focusedId.equals(returnFocusRoot.id))
			return 72;

		var popupDismissals = 0;
		var popup = new Popup("popup-smoke", new Button("Popup action"), 100.0, 60.0,
			null, function() { popupDismissals++; });
		var popupRoot = context.submit(popup, dialogFrame);
		context.pointerDown(5.0, 5.0, 0);
		context.pointerUp(5.0, 5.0, 0);
		if (popupDismissals != 1 || popupRoot.children[0].resolved == null)
			return 73;

		var selectedMenuItem = "";
		var menuDismissals = 0;
		var menu = new Menu("menu-smoke", [
			new MenuItem("open", "Open", function() { selectedMenuItem = "open"; }),
			new MenuItem("disabled", "Unavailable", null, false)
		], 32.0, 24.0, function() { menuDismissals++; });
		var menuRoot = context.submit(menu, dialogFrame);
		if (context.focus.focusedId == null)
			return 74;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (selectedMenuItem != "open" || menuDismissals != 1 ||
			menuRoot.children[1].children[0].children[1].enabled)
			return 75;

		var tooltip = new Tooltip("tooltip-smoke", new Button("Anchor"), new Text("Hint"));
		var tooltipFrame = new LayoutFrame(256.0, 192.0);
		var tooltipRoot = context.submit(tooltip, tooltipFrame);
		var tooltipGeometry:ResolvedLayoutItem = cast tooltipRoot.children[1].resolved;
		var tooltipRootGeometry:ResolvedLayoutItem = cast tooltipRoot.resolved;
		var anchorGeometry:ResolvedLayoutItem = cast tooltipRoot.children[0].resolved;
		if (tooltipRootGeometry.height < anchorGeometry.height)
			return 101;
		if (tooltipGeometry.visible)
			return 76;
		context.pointerMove(anchorGeometry.x + 4.0, anchorGeometry.y + 4.0);
		tooltipRoot = context.submit(tooltip, tooltipFrame);
		tooltipGeometry = cast tooltipRoot.children[1].resolved;
		if (!tooltipGeometry.visible)
			return 77;
		context.pointerMove(250.0, 190.0);
		tooltipRoot = context.submit(tooltip, tooltipFrame);
		tooltipGeometry = cast tooltipRoot.children[1].resolved;
		if (tooltipGeometry.visible)
			return 78;
		var tooltipCardStyle = new LayoutStyle();
		tooltipCardStyle.width = LayoutAxis.fixed(220.0);
		tooltipCardStyle.height = LayoutAxis.fit();
		tooltipCardStyle.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		tooltipCardStyle.childGap = 6.0;
		var tooltipCard = new Column("tooltip-card", [
			new KeyedView("heading", new Text("Open an overlay")),
			new KeyedView("anchor", new Tooltip("tooltip-in-card",
				new Button("Hover for tooltip"), new Text("Hint")))
		], tooltipCardStyle);
		var tooltipCardRoot = context.submit(tooltipCard, tooltipFrame);
		var tooltipCardGeometry:ResolvedLayoutItem = cast tooltipCardRoot.resolved;
		var tooltipCardAnchor:ResolvedLayoutItem = cast tooltipCardRoot.children[1].children[0].resolved;
		if (tooltipCardAnchor.y + tooltipCardAnchor.height >
			tooltipCardGeometry.y + tooltipCardGeometry.height - 12.0 + 0.1)
			return 103;

		var radioChanges = 0;
		var radioValue = "";
		var radioGroup = new RadioGroup("radio-smoke", [
			new RadioOption("red", "Red", "red"),
			new RadioOption("blocked", "Blocked", "blocked", false),
			new RadioOption("green", "Green", "green")
		], "red", function(next) { radioChanges++; radioValue = next; });
		var radioFrame = new LayoutFrame(256.0, 192.0);
		var radioRoot = context.submit(radioGroup, radioFrame);
		if (radioRoot.children.length != 3 ||
			(cast(radioRoot.children[0].semantics, Semantics).states & AccessibilityState.Selected) == 0 ||
			!context.focusWidget(radioRoot.children[0].id))
			return 92;
		context.key(UiEventKind.KeyDown, UiKey.Right);
		if (radioGroup.value != "green" || radioValue != "green" || radioChanges != 1 ||
			context.focus.focusedId == null ||
			!context.focus.focusedId.equals(radioRoot.children[2].id))
			return 93;
		radioRoot = context.submit(radioGroup, radioFrame);
		if ((cast(radioRoot.children[2].semantics, Semantics).states & AccessibilityState.Selected) == 0)
			return 94;

		var tabChanges = 0;
		var tabs = new Tabs("tabs-smoke", [
			new TabItem("first", "First", new Text("First page")),
			new TabItem("second", "Second", new Text("Second page")),
			new TabItem("locked", "Locked", new Text("Locked page"), false)
		], "first", function(_) { tabChanges++; });
		var tabsFrame = new LayoutFrame(256.0, 192.0);
		var tabsRoot = context.submit(tabs, tabsFrame);
		var secondTab = tabsRoot.children[0].children[1];
		var secondTabGeometry:ResolvedLayoutItem = cast secondTab.resolved;
		context.pointerDown(secondTabGeometry.x + 2.0, secondTabGeometry.y + 2.0, 0);
		context.pointerUp(secondTabGeometry.x + 2.0, secondTabGeometry.y + 2.0, 0);
		if (tabs.selectedKey != "second" || tabChanges != 1)
			return 95;
		tabsRoot = context.submit(tabs, tabsFrame);
		var secondPage:Semantics = cast tabsRoot.children[1].semantics;
		if (secondPage.role != AccessibilityRole.TabPanel || secondPage.label != "Second" ||
			(cast(tabsRoot.children[0].children[1].semantics, Semantics).states &
			AccessibilityState.Selected) == 0)
			return 96;
		if (!context.focusWidget(tabsRoot.children[0].children[1].id))
			return 97;
		context.key(UiEventKind.KeyDown, UiKey.Left);
		if (tabs.selectedKey != "first" || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(tabsRoot.children[0].children[0].id))
			return 98;

		var theme = new Theme();
		theme.text = Color.rgba(0.10, 0.14, 0.21, 1.0);
		theme.buttonText = Color.rgba(1.0, 1.0, 1.0, 1.0);
		var lightNeutral = Color.rgba(0.87, 0.90, 0.95, 1.0);
		var accentButton = Color.rgba(0.18, 0.39, 0.70, 1.0);
		if (theme.buttonLabelColor(true, lightNeutral) != theme.text ||
			theme.buttonLabelColor(true, accentButton) != theme.buttonText ||
			theme.buttonLabelColor(false, accentButton) != theme.disabledButtonText)
			return 101;
		theme.buttonHover = Color.rgba(0.8, 0.1, 0.1, 1.0);
		theme.buttonPressed = Color.rgba(0.7, 0.05, 0.05, 1.0);
		theme.buttonFocused = Color.rgba(0.4, 0.2, 0.8, 1.0);
		theme.buttonDisabled = Color.rgba(0.2, 0.2, 0.2, 1.0);
		context.setTheme(theme);
		var themedClicks = 0;
		var themedButton = new Button("Themed", null, function() { themedClicks++; }, "theme-key");
		var themedFrame = new LayoutFrame(256.0, 192.0);
		var themedRoot = context.submit(themedButton, themedFrame);
		if (!context.focusWidget(themedRoot.id))
			return 80;
		themedRoot = context.submit(themedButton, themedFrame);
		if (themedRoot.layout.style.background.red != 0.4)
			return 81;
		var themedLabelGeometry:ResolvedLayoutItem = cast themedRoot.children[0].resolved;
		var themedX = themedLabelGeometry.x + themedLabelGeometry.width * 0.5;
		var themedY = themedLabelGeometry.y + themedLabelGeometry.height * 0.5;
		context.pointerMove(themedX, themedY);
		themedRoot = context.submit(themedButton, themedFrame);
		var themedSnapshot = context.inspect();
		if (themedRoot.layout.style.background.red != 0.8 || !themedSnapshot[0].hovered ||
			themedSnapshot[1].hovered)
			return 82;
		context.pointerDown(themedX, themedY, 0);
		themedRoot = context.submit(themedButton, themedFrame);
		themedSnapshot = context.inspect();
		if (themedRoot.layout.style.background.red != 0.7 || !themedSnapshot[0].pressed ||
			themedSnapshot[1].pressed)
			return 83;
		context.pointerUp(themedX, themedY, 0);
		if (themedClicks != 1 || context.inspect()[0].pressed)
			return 84;
		themedButton.enabled = false;
		themedRoot = context.submit(themedButton, themedFrame);
		if (themedRoot.layout.style.background.red != 0.2)
			return 85;
		var inspected = context.inspect();
		if (inspected.length != 2 || inspected[0].label != "Themed" ||
			inspected[1].parentId != inspected[0].id || context.dumpTree().length == 0 ||
			context.auditAccessibility().length != 0)
			return 99;
		var unnamedSemantics:Semantics = cast themedRoot.semantics;
		unnamedSemantics.label = "";
		if (AccessibilityAudit.isValid(themedRoot))
			return 100;

		var taps = 0;
		var doubleTaps = 0;
		var longPresses = 0;
		var dragStarts = 0;
		var dragMoves = 0;
		var dragEnds = 0;
		var gestures = new GestureDetector("gesture-smoke",
			new SizedBox("gesture-area", new Text("Drag here"), LayoutAxis.fixed(100.0),
				LayoutAxis.fixed(60.0)), [
			new TapRecognizer(function(_:GestureEvent) { taps++; }),
			new DoubleTapRecognizer(function(_:GestureEvent) { doubleTaps++; }),
			new LongPressRecognizer(function(_:GestureEvent) { longPresses++; }),
			new DragRecognizer(6.0,
				function(_:GestureEvent) { dragStarts++; },
				function(_:GestureEvent) { dragMoves++; },
				function(_:GestureEvent) { dragEnds++; })
		]);
		var gestureFrame = new LayoutFrame(256.0, 192.0);
		var gestureRoot = context.submit(gestures, gestureFrame);
		var gestureGeometry:ResolvedLayoutItem = cast gestureRoot.children[0].resolved;
		var gestureX = gestureGeometry.x + 10.0;
		var gestureY = gestureGeometry.y + 10.0;
		context.pointerDown(gestureX, gestureY, 0);
		context.pointerUp(gestureX, gestureY, 0);
		gestureFrame.deltaSeconds = 0.1;
		gestureRoot = context.submit(gestures, gestureFrame);
		gestureFrame.deltaSeconds = 0.0;
		context.pointerDown(gestureX, gestureY, 0);
		context.pointerUp(gestureX, gestureY, 0);
		if (taps != 2 || doubleTaps != 1)
			return 86;
		context.pointerDown(gestureX, gestureY, 0);
		gestureFrame.deltaSeconds = 0.6;
		gestureRoot = context.submit(gestures, gestureFrame);
		gestureFrame.deltaSeconds = 0.0;
		context.pointerUp(gestureX, gestureY, 0);
		if (longPresses != 1 || taps != 2)
			return 87;
		context.pointerDown(gestureX, gestureY, 0);
		context.pointerMove(gestureX + 20.0, gestureY + 10.0);
		context.pointerUp(gestureX + 20.0, gestureY + 10.0, 0);
		if (dragStarts != 1 || dragMoves == 0 || dragEnds != 1 || taps != 2)
			return 88;

		var animationUpdates = 0;
		var animationCompletions = 0;
		var animation = new AnimationController(context.animations,
			function(_) { animationUpdates++; }, function() { animationCompletions++; });
		animation.play(0.0, 100.0, 1.0, Easing.EaseInOut);
		var animationFrame = new LayoutFrame(256.0, 192.0);
		animationFrame.deltaSeconds = 0.5;
		context.submit(new Text("Tween"), animationFrame);
		if (animation.value < 49.9 || animation.value > 50.1 || animationUpdates != 1)
			return 89;
		context.submit(new Text("Tween"), animationFrame);
		if (animation.value != 100.0 || animation.active || animationCompletions != 1)
			return 90;
		var spring = new SpringController(0.0, 180.0, 24.0, 1.0, 0.001,
			context.animations);
		spring.setTarget(1.0);
		animationFrame.deltaSeconds = 1.0 / 60.0;
		for (_ in 0...120)
			context.submit(new Text("Spring"), animationFrame);
		if (spring.value < 0.99 || spring.active || context.animations.activeCount != 0)
			return 91;
		var restart:AnimationController = null;
		var restartCompletions = 0;
		restart = new AnimationController(context.animations, null, function() {
			restartCompletions++;
			if (restartCompletions == 1)
				restart.play(0.0, 1.0, 0.1);
		});
		restart.play(0.0, 1.0, 0.1);
		animationFrame.deltaSeconds = 0.1;
		context.submit(new Text("Restart"), animationFrame);
		if (restartCompletions != 1 || !restart.active || context.animations.activeCount != 1)
			return 119;
		context.submit(new Text("Restart"), animationFrame);
		if (restartCompletions != 2 || restart.active || context.animations.activeCount != 0)
			return 121;
		var loop = new LoopAnimation();
		var firstHandle = context.animations.track(loop);
		var secondHandle = context.animations.track(loop);
		if (firstHandle != secondHandle || context.animations.activeCount != 1)
			return 117;
		firstHandle.cancel();
		if (firstHandle.active || context.animations.activeCount != 0)
			return 118;

		var spinner = new Spinner("spinner-smoke", "Loading", null, SpinnerKind.Dots,
			Color.rgba(0.3, 0.7, 1.0, 1.0), 1.5);
		var spinnerFrame = new LayoutFrame(256.0, 192.0);
		var animationRequests = 0;
		context.onAnimationFrameRequested = function() { animationRequests++; };
		var spinnerRoot = context.submit(spinner, spinnerFrame);
		var spinnerSemantics:Semantics = cast spinnerRoot.semantics;
		if (spinnerRoot.resolved == null || spinnerRoot.resolved.width != 24.0 ||
			spinnerRoot.resolved.height != 24.0 || spinnerSemantics == null ||
			spinnerSemantics.role != AccessibilityRole.ProgressBar ||
			spinnerSemantics.label != "Loading" ||
			(spinnerSemantics.states & AccessibilityState.Busy) == 0 ||
			context.animations.activeCount != 1 || animationRequests == 0 ||
			!context.needsAnimationFrame || !context.isDirty())
			return 109;
		spinnerFrame.deltaSeconds = 0.25;
		var requestsBeforeFrame = animationRequests;
		context.submit(spinner, spinnerFrame);
		if (context.animations.activeCount != 1 || animationRequests <= requestsBeforeFrame)
			return 110;
		spinner.running = false;
		spinnerFrame.deltaSeconds = 0.0;
		spinnerRoot = context.submit(spinner, spinnerFrame);
		spinnerSemantics = cast spinnerRoot.semantics;
		if (context.animations.activeCount != 0 ||
			(spinnerSemantics.states & AccessibilityState.Busy) != 0 ||
			spinnerSemantics.value != "Paused" || context.needsAnimationFrame || context.isDirty())
			return 111;
		spinner.running = true;
		spinnerRoot = context.submit(spinner, spinnerFrame);
		if (context.animations.activeCount != 1)
			return 112;
		var spinnerId = spinnerRoot.id;
		context.onAnimationFrameRequested = null;
		context.submit(new Text("Unmount"), spinnerFrame);
		if (context.stateStore.contains(spinnerId) || context.animations.activeCount != 0)
			return 112;

		var spinnerGeometry = new ResolvedLayoutItem(1, 1, 0.0, 0.0, 24.0, 24.0,
			new Rect(0.0, 0.0, 24.0, 24.0), new Rect(0.0, 0.0, 24.0, 24.0),
			Transform2D.identity(), 0.0);
		var spinnerPainter = new SpinnerPainter(context.animations,
			Color.rgba(0.3, 0.7, 1.0, 1.0), 1.0);
		var spinnerCanvas = new Canvas();
		var spinnerList = DisplayList.create();
		spinnerPainter.paintFrame(spinnerCanvas, spinnerGeometry);
		spinnerCanvas.update(spinnerList);
		if (spinnerList.info().commandCount <= 0)
			return 113;
		spinnerList.clear();
		spinnerCanvas.reset();
		spinnerPainter.configure(SpinnerKind.Dots, Color.rgba(0.3, 0.7, 1.0, 1.0), 1.0);
		spinnerPainter.paintFrame(spinnerCanvas, spinnerGeometry);
		spinnerCanvas.update(spinnerList);
		if (spinnerList.info().commandCount <= 0)
			return 114;
		spinnerList.clear();
		spinnerCanvas.reset();
		spinnerPainter.configure(SpinnerKind.Bars, Color.rgba(0.3, 0.7, 1.0, 1.0), 1.0);
		spinnerPainter.paintFrame(spinnerCanvas, spinnerGeometry);
		spinnerCanvas.update(spinnerList);
		if (spinnerList.info().commandCount <= 0)
			return 115;
		spinnerList.clear();
		spinnerCanvas.reset();
		spinnerPainter.configure(SpinnerKind.Pulse, Color.rgba(0.3, 0.7, 1.0, 1.0), 1.0);
		spinnerPainter.paintFrame(spinnerCanvas, spinnerGeometry);
		spinnerCanvas.update(spinnerList);
		if (spinnerList.info().commandCount <= 0)
			return 116;
		spinnerList.clear();
		spinnerCanvas.reset();
		spinnerList.dispose();
		spinnerPainter.dispose();
		var accessibilityResult = AccessibilityContract.run(fonts);
		if (accessibilityResult != 0)
			return 120 + accessibilityResult;

		var degenerateCanvas = new Canvas();
		var degenerateList = DisplayList.create();
		if (degenerateCanvas.fillRectIfPositive(new Rect(0.0, 0.0, 0.0, 4.0),
			Color.rgba(0.2, 0.4, 0.8, 0.5)) ||
			degenerateCanvas.fillRectIfPositive(new Rect(0.0, 0.0, 4.0, -1.0),
			Color.rgba(0.2, 0.4, 0.8, 0.5)))
			return 107;
		degenerateCanvas.update(degenerateList);
		if (degenerateList.info().commandCount != 0)
			return 108;
		degenerateList.dispose();

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
		Sys.println("PASS: Haxe framework, 4,000-node layout pressure, and NativeKit input routing");
		return 0;
	}
}
