import Color;
import Canvas;
import DisplayList;
import CompositeMode;
import FontCollection;
import GradientStop;
import Image;
import ImageFormat;
import ImageFilter;
import Insets;
import Path;
import SolidPaint;
import nativekit.ui.icons.IconName;
import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutPositioning;
import LayoutSizing;
import LayoutStyle;
import LayoutSizing;
import LayoutVisualKind;
import TextAlignment;
import Rect;
import Point;
import ResolvedLayoutItem;
import Transform2D;
import TextLayout;
import TextDirection;
import TextStyle;
import TextWrap;
import NativeKit.InputAction;
import NativeKit.InitOptions;
import NativeKit.WindowDecorationRegionKind;
import NativeKit.WindowFlags;
import NativeKit.WindowKind;
import NativeKit.WindowOptions;
import NativeKit.TouchAction;
import NativeKit.TouchTool;
import NativeKit.TextEditAction;
import NativeKitEventValue;
import NativeKitEventValue.NativeKitTextEdit;
import NativeKitEvents;
import NativeKitRuntime;
import NativeKitEventDecoderTests;
import nativekit.ui.core.NativeInputAdapter;
import nativekit.ui.core.CursorShape as UiCursorShape;
import nativekit.ui.core.CachePolicy;
import nativekit.ui.core.EventDispatcher;
import nativekit.ui.core.FocusManager;
import nativekit.ui.core.HitTest;
import nativekit.ui.core.HitTestBehavior;
import nativekit.ui.core.InteractionStateStore;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiDirtyFlag;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.UiTouchData;
import nativekit.ui.core.WidgetId;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.ButtonVariant;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.AppShell;
import nativekit.ui.widgets.CanvasView;
import nativekit.ui.widgets.Shape;
import nativekit.ui.widgets.Checkbox;
import nativekit.ui.widgets.ImageView;
import nativekit.ui.widgets.LayeredImageView;
import nativekit.ui.widgets.LayeredImageView.ImageLayer;
import nativekit.ui.widgets.NineSliceView;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ListView;
import nativekit.ui.widgets.ListViewModel;
import nativekit.ui.widgets.Padding;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.DefaultTextStyle;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.Popup;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.SearchField;
import nativekit.ui.widgets.SizedBox;
import nativekit.ui.widgets.Text;
import nativekit.ui.core.TextStyleOverride;
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
import nativekit.ui.widgets.SplitOrientation;
import nativekit.ui.widgets.SplitSide;
import nativekit.ui.widgets.SplitView;
import nativekit.ui.widgets.SplitViewOptions;
import nativekit.ui.widgets.TableColumn;
import nativekit.ui.widgets.TableView;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.Tooltip;
import nativekit.ui.widgets.TreeView;
import nativekit.ui.widgets.TreeViewModel;
import nativekit.ui.widgets.Utf8Text;
import nativekit.ui.widgets.VirtualGrid;
import nativekit.ui.widgets.VirtualList;
import nativekit.ui.widgets.VirtualExtentViewport;
import nativekit.ui.widgets.VirtualizationPolicy;
import nativekit.ui.widgets.VirtualViewport;
import nativekit.ui.widgets.WindowChrome;
import nativekit.ui.theme.Theme;
import nativekit.ui.theme.ThemeTokens;
import nativekit.ui.theme.TextRole;
import nativekit.ui.style.ComputedStyle;
import nativekit.ui.style.DecorationChain;
import nativekit.ui.style.Decoration;
import nativekit.ui.style.BackgroundDecoration;
import nativekit.ui.style.BorderDecoration;
import nativekit.ui.style.GradientDecoration;
import nativekit.ui.style.ShadowDecoration;
import nativekit.ui.style.StyleDiff;
import nativekit.ui.style.StyleResolver;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleImpact;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleDiff;
import nativekit.ui.style.StyleSelector;
import nativekit.ui.style.StyleSheet;
import nativekit.ui.style.StyleSource;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.style.StyleValue;
import nativekit.ui.style.EffectChain;
import nativekit.ui.style.EffectKind;
import nativekit.ui.style.BlurEffect;
import nativekit.ui.style.BrightnessEffect;
import nativekit.ui.style.ContrastEffect;
import nativekit.ui.style.SaturateEffect;
import nativekit.ui.style.HueRotateEffect;
import nativekit.ui.style.ColorMatrixEffect;
import nativekit.ui.style.DropShadowEffect;
import nativekit.ui.style.CustomEffect;
import nativekit.ui.style.CustomEffectDefinition;
import nativekit.ui.style.EffectParameter;
import nativekit.ui.style.EffectParameterType;
import nativekit.ui.style.InkOverflow;
import nativekit.ui.style.Mask;
import nativekit.ui.style.Environment;
import nativekit.ui.style.EnvironmentColorScheme;
import nativekit.ui.style.StyleEnvironment;
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
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.animation.SpringController;
import nativekit.ui.animation.LoopAnimation;
import nativekit.ui.animation.Easing;
import nativekit.ui.debug.UiInspector;
import nativekit.ui.debug.UiFrameMetrics;
import nativekit.ui.debug.AccessibilityAudit;
import AccessibilityContract;

class FrameworkSmoke {
	static function main():Int {
		if (!coordinateMathValid())
			return 240;
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
		emptyField.placeholder = "Type a message…";
		var emptyRoot = context.submit(emptyField, new LayoutFrame(256.0, 192.0));
		if (emptyRoot.semantics == null || emptyRoot.semantics.value != "" ||
			emptyRoot.children[0].children[1].layout.text != "Type a message…")
			return 39;
		var searchChange = "unchanged";
		var search = new SearchField("search", "query", function(next) searchChange = next,
			null, "Search components…");
		var searchRoot = context.submit(search, new LayoutFrame(256.0, 38.0));
		var searchIcon = searchRoot.children[0].children[0];
		var searchInputSemantics:Semantics = cast searchRoot.children[1].semantics;
		var searchClearSemantics:Semantics = cast searchRoot.children[2].semantics;
		if (searchRoot.children.length != 3 ||
			searchRoot.children[0].layout.style.width.value != 18.0 ||
			searchIcon.layout.visualKind != LayoutVisualKind.Custom ||
			searchIcon.hitTestSelf ||
			searchInputSemantics.role != AccessibilityRole.TextField ||
			searchInputSemantics.label != "Search components…" ||
			searchClearSemantics.label != "Clear search")
			return 227;
		if (!context.accessibilityAction(searchRoot.children[2].id.value,
			AccessibilityAction.Activate, null, -1, -1, 1) || search.value != "" ||
			searchChange != "")
			return 228;
		searchRoot = context.submit(search, new LayoutFrame(256.0, 38.0));
		if (searchRoot.children.length != 2 ||
			searchRoot.children[1].children[0].children[1].layout.text != "Search components…")
			return 229;
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
		var labeledIconButton = new Button("Search", null, null, "labeled-icon-smoke");
		labeledIconButton.leadingIcon = IconName.Search;
		var labeledIconRoot = context.submit(labeledIconButton, new LayoutFrame(256.0, 192.0));
		var labeledIconSemantics:Semantics = cast labeledIconRoot.semantics;
		if (labeledIconRoot.children.length != 2 || labeledIconSemantics == null ||
			labeledIconSemantics.label != "Search")
			return 236;
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
		var indeterminateProgress = new ProgressBar("indeterminate-progress-smoke", 0.0,
			0.0, 1.0, "Loading");
		indeterminateProgress.mode = nativekit.ui.widgets.ProgressMode.Indeterminate;
		var indeterminateRoot = context.submit(indeterminateProgress,
			new LayoutFrame(256.0, 192.0));
		var indeterminateSemantics:Semantics = cast indeterminateRoot.semantics;
		if (indeterminateSemantics == null ||
			(indeterminateSemantics.states & AccessibilityState.Busy) == 0 ||
			(indeterminateSemantics.states & AccessibilityState.ReadOnly) != 0 ||
			context.animations.activeCount == 0)
			return 232;
		var imageBytes = haxe.io.Bytes.alloc(16);
		for (index in 0...16)
			imageBytes.set(index, 255);
		var image = Image.create(2, 2, ImageFormat.RGBA8, imageBytes);
		var loadedImage = Image.loadFile(Sys.getEnv("NKUI_TEST_IMAGE_PATH"), ImageFilter.Nearest);
		if (loadedImage.width != 22 || loadedImage.height != 22 ||
			loadedImage.filter != ImageFilter.Nearest)
			return 239;
		loadedImage.dispose();
		var imageRoot = context.submit(new ImageView("image-smoke", image, "Picture"),
			new LayoutFrame(256.0, 192.0));
		var imageSemantics:Semantics = cast imageRoot.semantics;
		var imageGeometry:ResolvedLayoutItem = cast imageRoot.resolved;
		if (imageSemantics.role != AccessibilityRole.Image || imageSemantics.label != "Picture" ||
			imageGeometry.width != 2.0 || imageGeometry.height != 2.0)
			return 64;
		var layeredRoot = context.submit(new LayeredImageView("layers-smoke", [
			new ImageLayer(image, 0.0, 0.0, 1.0, 1.0),
			new ImageLayer(image, 0.25, 0.25, 0.5, 0.5, 0.5)
		], "Layered picture"), new LayoutFrame(256.0, 192.0));
		var layeredSemantics:Semantics = cast layeredRoot.semantics;
		if (layeredSemantics == null || layeredSemantics.role != AccessibilityRole.Image ||
			layeredSemantics.label != "Layered picture")
			return 237;
		var nineSliceRoot = context.submit(new NineSliceView("nine-slice-smoke", image,
			1.0, 1.0, 1.0, 1.0, "Scalable frame"), new LayoutFrame(256.0, 192.0));
		var nineSliceSemantics:Semantics = cast nineSliceRoot.semantics;
		var nineSliceDecorations:DecorationChain = nineSliceRoot.computedStyle == null ? null :
			nineSliceRoot.computedStyle.get(StyleProperty.Decorations);
		var nineSliceDecorationSource:Null<StyleSource> = nineSliceRoot.computedStyle == null ? null :
			nineSliceRoot.computedStyle.source(StyleProperty.Decorations);
		if (nineSliceSemantics == null || nineSliceSemantics.role != AccessibilityRole.Image ||
			nineSliceSemantics.label != "Scalable frame" || nineSliceDecorations == null ||
			nineSliceDecorations.decorations.length != 1 || nineSliceDecorationSource == null ||
			nineSliceDecorationSource.layer != "default")
			return 238;
		var imageCanvas = new Canvas();
		var imageList = DisplayList.create();
		imageCanvas.drawImage(image, new Rect(1.0, 2.0, 20.0, 12.0));
		imageCanvas.update(imageList);
		if (imageList.info().commandCount != 1)
			return 65;
		imageList.clear();
		imageList.dispose();
		image.dispose();
		var shadowStyle = new ComputedStyle();
		shadowStyle.set(StyleProperty.ShadowColor, Color.rgba(0.05, 0.08, 0.12, 0.7), null);
		shadowStyle.set(StyleProperty.ShadowOffsetX, 2.0, null);
		shadowStyle.set(StyleProperty.ShadowOffsetY, 3.0, null);
		shadowStyle.set(StyleProperty.ShadowBlur, 9.0, null);
		shadowStyle.set(StyleProperty.ShadowSpread, 2.0, null);
		shadowStyle.set(StyleProperty.RadiusTopLeft, 3.0, null);
		shadowStyle.set(StyleProperty.RadiusTopRight, 5.0, null);
		shadowStyle.set(StyleProperty.RadiusBottomRight, 7.0, null);
		shadowStyle.set(StyleProperty.RadiusBottomLeft, 9.0, null);
		var shadowGeometry = new ResolvedLayoutItem(501, 1, 0.0, 0.0, 80.0, 40.0,
			new Rect(0.0, 0.0, 80.0, 40.0), new Rect(0.0, 0.0, 80.0, 40.0),
			Transform2D.identity(), 0.0);
		var shadowCanvas = new Canvas();
		var shadowList = DisplayList.create();
		new ShadowDecoration().paint(shadowCanvas, shadowGeometry, shadowStyle);
		shadowCanvas.update(shadowList);
		var shadowInfo = shadowList.info();
		if (shadowInfo.commandCount != 1 || shadowInfo.commandBytes != 72)
			return 240;
		shadowList.dispose();
		var canvasEvents = 0;
		var canvasView = new CanvasView("canvas-smoke", function(canvas, geometry) {
			canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
				Color.rgba(0.3, 0.4, 0.5, 1.0));
		}, null, "Drawing region", true, CachePolicy.Raster, "canvas-static");
		canvasView.on(UiEventKind.PointerDown, function(_) { canvasEvents++; });
		var canvasRoot = context.submit(canvasView, new LayoutFrame(256.0, 192.0));
		var canvasGeometry:ResolvedLayoutItem = cast canvasRoot.resolved;
		var canvasSemantics:Semantics = cast canvasRoot.semantics;
		context.pointerDown(canvasGeometry.x + 10.0, canvasGeometry.y + 10.0, 0);
		if (canvasEvents != 1 || canvasSemantics.role != AccessibilityRole.Group ||
			canvasRoot.cachePolicy != CachePolicy.Raster)
			return 66;
		var simpleCanvas = CanvasView.simple("simple-canvas", function(canvas) {
			canvas.fillRect(new Rect(0.0, 0.0, 12.0, 12.0), Color.rgba(0.8, 0.2, 0.2, 1.0));
		}, null, "Simple drawing", true, CachePolicy.Raster, "simple-static");
		var simpleCanvasRoot = context.submit(simpleCanvas, new LayoutFrame(64.0, 64.0));
		if (simpleCanvasRoot.cachePolicy != CachePolicy.Raster)
			return 241;
		var shapePath = new PathBuilder().roundRect(0.0, 0.0, 32.0, 20.0, 4.0).build();
		var shapePaint = SolidPaint.create(Color.rgba(0.2, 0.7, 0.4, 1.0));
		var shape = Shape.fill("shape-smoke", shapePath, shapePaint, null, "Filled shape",
			true, CachePolicy.Raster, "shape-static");
		var shapeRoot = context.submit(shape, new LayoutFrame(64.0, 64.0));
		var shapeSemantics:Semantics = cast shapeRoot.semantics;
		if (shapeSemantics == null || shapeSemantics.role != AccessibilityRole.Group ||
			shapeSemantics.label != "Filled shape" || shapeRoot.cachePolicy != CachePolicy.Raster)
			return 242;
		shapePath.dispose();
		shapePaint.dispose();
		var responsiveCanvasStyle = new LayoutStyle();
		responsiveCanvasStyle.width = LayoutAxis.stretch();
		responsiveCanvasStyle.height = LayoutAxis.fixed(48.0);
		var responsiveCanvas = new CanvasView("responsive-canvas", function(canvas, geometry) {
			canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height),
				Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, responsiveCanvasStyle, "Responsive drawing");
		var responsiveCanvasRoot = context.submit(responsiveCanvas, new LayoutFrame(256.0, 80.0));
		var responsiveCanvasGeometry:ResolvedLayoutItem = cast responsiveCanvasRoot.resolved;
		if (responsiveCanvasGeometry.width != 256.0 || responsiveCanvasGeometry.height != 48.0)
			return 231;
		if (!NativeKitEventDecoderTests.run())
			return 27;
		var frame = new LayoutFrame(256.0, 192.0);
		var cycleRoot = new RenderNode(new WidgetId(0x7fffff00));
		var cycleChild = new RenderNode(new WidgetId(0x7fffff01));
		cycleRoot.children.push(cycleChild);
		cycleChild.children.push(cycleRoot);
		if (cycleRoot.find(new WidgetId(0x7fffff02)) != null)
			return 105;
		var rejectedBoxPaint = false;
		try {
			new RenderNode(new WidgetId(0x7fffff03), LayoutVisualKind.Box)
				.onPaint(function(_, _) {});
		} catch (_:Dynamic) {
			rejectedBoxPaint = true;
		}
		if (!rejectedBoxPaint)
			return 228;
		var clicks = 0;
		var suppressNextClick = false;
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
		root.on(UiEventKind.PointerDown, function(event) {
			if (suppressNextClick) {
				event.preventDefault();
				event.stopPropagation();
			}
		}, "capture");
		var buttonNode = root.children[0];
		var initialId = buttonNode.id;
		root.hitTestBehavior = HitTestBehavior.ChildrenOnly;
		if (HitTest.path(root, 4.0, 4.0).length < 2)
			return 241;
		root.hitTestBehavior = HitTestBehavior.SelfOnly;
		if (HitTest.path(root, 4.0, 4.0).length != 1)
			return 242;
		root.hitTestBehavior = HitTestBehavior.None;
		if (HitTest.path(root, 4.0, 4.0).length != 0)
			return 243;
		root.hitTestBehavior = HitTestBehavior.Auto;
		var state:State<Int> = context.buildContext.state(buttonNode.id, 0);
		if (root.children.length != 3 || buttonNode.resolved == null ||
			!buttonNode.focusable || !buttonNode.resolved.hitTest(4.0, 4.0) || context.isDirty())
			return 3;
		if (context.dirtyFlags != UiDirtyFlag.None)
			return 221;
		var rootLocalX = -1.0;
		var buttonLocalX = -1.0;
		root.on(UiEventKind.PointerDown, function(event) {
			if (event.data == "coordinate-test")
				rootLocalX = event.localX;
		}, "capture");
		buttonNode.on(UiEventKind.PointerDown, function(event) {
			if (event.data == "coordinate-test") {
				buttonLocalX = event.localX;
				event.preventDefault();
			}
		});
		var coordinatePoint = new Point(6.0, 7.0);
		var coordinateGlobal = buttonNode.localToGlobal(coordinatePoint);
		context.pointerDown(coordinateGlobal.x, coordinateGlobal.y, 0, 0, 0, "coordinate-test");
		context.pointerUp(coordinateGlobal.x, coordinateGlobal.y, 0, 0, 0, "coordinate-test");
		var expectedRootPoint = root.globalToLocal(coordinateGlobal);
		if (!near(rootLocalX, expectedRootPoint.x) || !near(buttonLocalX, coordinatePoint.x) ||
			!near(coordinateGlobal.x, buttonNode.localToGlobal(coordinatePoint).x))
			return 244;
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
		suppressNextClick = true;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(4.0, 4.0, 0);
		if (clicks != 1 || bubbled != 1 || captured != 1)
			return 254;
		state.update(7);
		if (!context.isDirty() || !UiDirtyFlag.contains(context.dirtyFlags, UiDirtyFlag.NeedsBuild) ||
			!UiDirtyFlag.contains(context.dirtyFlags, UiDirtyFlag.NeedsLayout))
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
		if (pumpEvents != 1 || hoverEnters != 0 || context.events.hoveredId() == null ||
			!context.events.hoveredId().equals(initialId))
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
		if (!input.consume(PointerMove(source, 4.0, 4.0)) || hoverEnters != 0 ||
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
		var sameTargetHandlers = 0;
		var immediateTargetHandlers = 0;
		var parentHandlers = 0;
		var eventStyle = new LayoutStyle();
		eventStyle.width = LayoutAxis.fixed(256.0);
		eventStyle.height = LayoutAxis.fixed(192.0);
		var eventRootView = new Column("event-semantics", [
			new KeyedView("stop", new Button("Stop propagation")),
			new KeyedView("immediate", new Button("Stop immediate"))
		], eventStyle);
		var eventRoot = context.submit(eventRootView, frame);
		var stopNode = eventRoot.children[0];
		var immediateNode = eventRoot.children[1];
		stopNode.on(UiEventKind.Click, function(event) {
			event.stopPropagation();
		}, "target");
		stopNode.on(UiEventKind.Click, function(_) {
			sameTargetHandlers++;
		}, "target");
		immediateNode.on(UiEventKind.Click, function(event) {
			event.stopImmediatePropagation();
		}, "target");
		immediateNode.on(UiEventKind.Click, function(_) {
			immediateTargetHandlers++;
		}, "target");
		eventRoot.on(UiEventKind.Click, function(_) {
			parentHandlers++;
		});
		var stopGeometry:ResolvedLayoutItem = cast stopNode.resolved;
		context.pointerDown(stopGeometry.x + 2.0, stopGeometry.y + 2.0, 0);
		context.pointerUp(stopGeometry.x + 2.0, stopGeometry.y + 2.0, 0);
		var immediateGeometry:ResolvedLayoutItem = cast immediateNode.resolved;
		context.pointerDown(immediateGeometry.x + 2.0, immediateGeometry.y + 2.0, 0);
		context.pointerUp(immediateGeometry.x + 2.0, immediateGeometry.y + 2.0, 0);
		if (sameTargetHandlers != 1 || immediateTargetHandlers != 0 || parentHandlers != 0)
			return 223;

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
			new KeyedView("message", new Button("Scrollable content"))
		], longContentStyle), viewportStyle, ScrollAxis.Vertical);
		var scrollFrame = new LayoutFrame(256.0, 80.0);
		var scrollRoot = context.submit(scrollView, scrollFrame);
		var contentGeometry:ResolvedLayoutItem = cast scrollRoot.children[0].resolved;
		if (scrollView.controller.maxScrollY != 320.0 || contentGeometry == null ||
			contentGeometry.clipBounds.height != 80.0)
			return 25;
		context.scroll(4.0, 4.0, 0.0, 50.0);
		if (scrollView.controller.offsetY != 50.0 || !context.isDirty())
			return 26;
		scrollRoot = context.submit(scrollView, scrollFrame);
		contentGeometry = cast scrollRoot.children[0].resolved;
		if (contentGeometry.transform.ty != -50.0)
			return 27;
		var nestedScrollControl = scrollRoot.children[0].children[0].children[0];
		nestedScrollControl.on(UiEventKind.KeyDown, function(event) {
			if (event.key == UiKey.Down)
				event.preventDefault();
		});
		if (!context.focusWidget(nestedScrollControl.id))
			return 229;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		if (scrollView.controller.offsetY != 50.0)
			return 230;
		scrollView.controller.jumpTo(0.0, 500.0);
		if (scrollView.controller.offsetY != 320.0 || !context.isDirty())
			return 28;
		scrollRoot = context.submit(scrollView, scrollFrame);
		if (scrollRoot.children.length != 2 || scrollRoot.children[1].children.length != 1)
			return 242;
		var scrollbarThumb = scrollRoot.children[1].children[0];
		var scrollbarSemantics:Semantics = cast scrollbarThumb.semantics;
		if (scrollbarSemantics.role != AccessibilityRole.Slider ||
			scrollbarSemantics.numericValue != 320.0 ||
			scrollbarSemantics.numericMaximum != 320.0 ||
			!context.accessibilityAction(scrollbarThumb.id.value,
				AccessibilityRequest.Decrement, null, -1, -1, 1) ||
			scrollView.controller.offsetY >= 320.0)
			return 243;
		var scrollbarTrack:ResolvedLayoutItem = cast scrollRoot.children[1].resolved;
		context.pointerDown(scrollbarTrack.x + scrollbarTrack.width * 0.5,
			scrollbarTrack.y + 4.0, 0);
		context.pointerUp(scrollbarTrack.x + scrollbarTrack.width * 0.5,
			scrollbarTrack.y + 4.0, 0);
		if (scrollView.controller.offsetY != 0.0)
			return 246;
		scrollView.controller.jumpTo(0.0, 160.0);
		if (!context.focusWidget(scrollRoot.id))
			return 244;
		context.key(UiEventKind.KeyDown, UiKey.Home);
		if (scrollView.controller.offsetY != 0.0)
			return 245;
		var emptyViewport = new VirtualViewport(0, 32.0, 350.0, 0.0);
		var topViewport = new VirtualViewport(100000, 32.0, 350.0, 0.0);
		var middleViewport = new VirtualViewport(100000, 32.0, 350.0, 414.0 * 32.0);
		var bottomViewport = new VirtualViewport(100000, 32.0, 350.0, 100000.0 * 32.0);
		if (emptyViewport.count != 0 || topViewport.first != 0 || topViewport.last != 12 ||
			!topViewport.contains(0) || topViewport.contains(12) ||
			middleViewport.first != 413 || middleViewport.last != 426 ||
			bottomViewport.first != 99988 || bottomViewport.last != 100000 ||
			!bottomViewport.contains(99999) || bottomViewport.contains(99987))
			return 247;
		var extentViewport = new VirtualExtentViewport([40.0, 80.0, 120.0, 60.0],
			120.0, 120.0);
		var extentAtEnd = new VirtualExtentViewport([40.0, 80.0, 120.0, 60.0],
			120.0, 1000.0);
		var policy = new VirtualizationPolicy(2, 3);
		var policyFixed = policy.fixed(100000, 32.0, 350.0, 414.0 * 32.0);
		var policyVariable = policy.variable([40.0, 80.0, 120.0, 60.0], 120.0, 120.0);
		if (extentViewport.totalExtent != 300.0 || extentViewport.first != 1 ||
			extentViewport.last != 4 || extentViewport.startOffset(2) != 120.0 ||
			extentViewport.count != 3 || extentAtEnd.offset != 180.0 ||
			!extentAtEnd.contains(3) || policyFixed.first != 412 || policyFixed.last != 428 ||
			policyVariable.first != 0 || policyVariable.last != 4 || !policy.recycleSlots)
			return 253;
		var gridController = new ScrollController();
		var gridStyle = new LayoutStyle();
		gridStyle.width = LayoutAxis.fixed(240.0);
		gridStyle.height = LayoutAxis.fixed(160.0);
		var builtCells:Array<String> = [];
		var virtualGrid = new VirtualGrid("grid-smoke", 1000, 100, 24.0, 80.0,
			function(row, column) {
				builtCells.push('$row:$column');
				return new Text('Cell $row,$column');
			}, gridStyle, null, gridController, 240.0, 160.0);
		var gridRoot = context.submit(virtualGrid, new LayoutFrame(240.0, 160.0));
		var gridSemantics:Semantics = cast gridRoot.semantics;
		if (gridSemantics.role != AccessibilityRole.Grid || gridSemantics.rowCount != 1000 ||
			gridSemantics.columnCount != 100 || builtCells.length == 0 || builtCells.length > 64)
			return 248;
		gridController.jumpTo(40.0 * 80.0, 500.0 * 24.0);
		builtCells.resize(0);
		gridRoot = context.submit(virtualGrid, new LayoutFrame(240.0, 160.0));
		if (builtCells.length == 0 || builtCells.length > 64)
			return 249;
		if (builtCells[0] != "499:39")
			return 250;
		var tableColumns:Array<TableColumn> = [
			new TableColumn("id", "ID", 60.0),
			new TableColumn("name", "Name", 100.0),
			new TableColumn("state", "State", 80.0)
		];
		var tableStyle = new LayoutStyle();
		tableStyle.width = LayoutAxis.fixed(240.0);
		tableStyle.height = LayoutAxis.fixed(184.0);
		var tableBuiltCells:Array<String> = [];
		var tableSelection = -1;
		var tableCellSelection = -1;
		var tableSortColumn = -1;
		var tableSortAscending = false;
		var tableResizeColumn = -1;
		var tableResizeWidth = 0.0;
		var table = new TableView("table-smoke", 100000, tableColumns, 24.0, function(row, column) {
			tableBuiltCells.push('$row:${column.key}');
			return new Text('$row ${column.label}');
		}, tableStyle, null, null, 240.0, 184.0, 24.0, 2, function(row:Int) {
			tableSelection = row;
		}, 0, function(row:Int, column:Int) {
			tableCellSelection = row * 10 + column;
		}, function(column:Int, ascending:Bool) {
			tableSortColumn = column;
			tableSortAscending = ascending;
		}, function(column:Int, width:Float) {
			tableResizeColumn = column;
			tableResizeWidth = width;
		});
		var tableRoot = context.submit(table, new LayoutFrame(240.0, 184.0));
		var tableSemantics:Semantics = cast tableRoot.semantics;
		var headerViewport = tableRoot.children[0];
		var headerContent = headerViewport.children[0];
		var firstHeaderSemantics:Null<Semantics> = headerContent.children[0].semantics;
		if (tableSemantics.role != AccessibilityRole.Grid || tableSemantics.rowCount != 100000 ||
			tableSemantics.columnCount != 3 || table.selectedRow != 2 || tableBuiltCells.length == 0 ||
			tableBuiltCells.length > 64 || firstHeaderSemantics == null ||
			firstHeaderSemantics.role != AccessibilityRole.ColumnHeader)
			return 251;
		context.pointerDown(12.0, 24.0 + 3.0 * 24.0 + 12.0, 0);
		context.pointerUp(12.0, 24.0 + 3.0 * 24.0 + 12.0, 0);
		if (table.selectedRow != 3 || tableSelection != 3)
			return 252;
		var selectedCell:Null<RenderNode> = null;
		tableRoot.walk(function(node) {
			if (node.semantics != null && node.semantics.role == AccessibilityRole.Cell &&
				node.semantics.rowIndex == 3 && node.semantics.columnIndex == 0)
				selectedCell = node;
		});
		if (selectedCell == null || !context.focusWidget(selectedCell.id))
			return 254;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		if (table.selectedRow != 4 || table.selectedColumn != 0 || tableCellSelection != 40)
			return 255;
		context.key(UiEventKind.KeyDown, UiKey.Right);
		if (table.selectedRow != 4 || table.selectedColumn != 1 || tableCellSelection != 41)
			return 256;
		context.pointerDown(12.0, 12.0, 0);
		context.pointerUp(12.0, 12.0, 0);
		if (table.sortColumn != 0 || !table.sortAscending || tableSortColumn != 0 ||
			!tableSortAscending)
			return 257;
		context.pointerDown(58.0, 12.0, 0);
		context.pointerMove(78.0, 12.0);
		context.pointerUp(78.0, 12.0, 0);
		if (table.columns[0].width != 80.0 || tableResizeColumn != 0 || tableResizeWidth != 80.0)
			return 258;
		tableRoot = context.submit(table, new LayoutFrame(240.0, 184.0));
		var resizedHeader = tableRoot.children[0].children[0].children[0].resolved;
		if (resizedHeader == null || resizedHeader.width != 80.0 || table.controller.maxScrollX != 20.0)
			return 259;
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
		var firstVirtualRowId = 0;
		virtualRoot.walk(function(node) {
			var semantics:Null<Semantics> = node.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.CollectionItem &&
				semantics.positionInSet == 1)
				firstVirtualRowId = node.id.value;
		});
		if (virtualSemantics.role != AccessibilityRole.Collection || virtualSemantics.setSize != 100 ||
			builtRows.length >= 12 ||
			listController.maxScrollY != 1920.0 || virtualList.materializedFirst != 0 ||
			virtualList.materializedLast != 6 || firstVirtualRowId == 0)
			return 67;
		listController.jumpTo(0.0, 500.0);
		builtRows.resize(0);
		virtualRoot = context.submit(virtualList, new LayoutFrame(256.0, 80.0));
		var recycledVirtualRowId = 0;
		virtualRoot.walk(function(node) {
			var semantics:Null<Semantics> = node.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.CollectionItem &&
				semantics.positionInSet == 25)
				recycledVirtualRowId = node.id.value;
		});
		if (builtRows.length >= 12 || builtRows.length == 0 || builtRows[0] < 24 ||
			builtRows[0] > 25 || listController.offsetY != 500.0 ||
			virtualList.materializedFirst != 24 || virtualList.materializedLast != 31 ||
			recycledVirtualRowId != firstVirtualRowId)
			return 68;
		var largeListController = new ScrollController();
		var largeVirtualStyle = new LayoutStyle();
		largeVirtualStyle.width = LayoutAxis.fixed(256.0);
		largeVirtualStyle.height = LayoutAxis.fixed(350.0);
		var largeBuiltRows:Array<Int> = [];
		var largeVirtualList = new VirtualList("large-virtual-smoke", 100000, 32.0, function(index) {
			largeBuiltRows.push(index);
			return new Text('Row ${index + 1}');
		}, largeVirtualStyle, null, largeListController, 350.0);
		context.submit(largeVirtualList, new LayoutFrame(256.0, 350.0));
		if (largeBuiltRows.length == 0 || largeBuiltRows.length > 16)
			return 104;
		largeListController.jumpTo(0.0, 414.0 * 32.0);
		largeBuiltRows.resize(0);
		var largeVirtualRoot = context.submit(largeVirtualList, new LayoutFrame(256.0, 350.0));
		if (largeBuiltRows.length == 0 || largeBuiltRows.length > 16 || largeBuiltRows[0] != 413 ||
			largeListController.offsetY != 13248.0)
			return 103;
		var modelBuiltRows:Array<Int> = [];
		var modelSelection = -1;
		var model = new SmokeListModel(100000, modelBuiltRows);
		var modelController = new ScrollController();
		var modelStyle = new LayoutStyle();
		modelStyle.width = LayoutAxis.fixed(256.0);
		modelStyle.height = LayoutAxis.fixed(120.0);
		var modelList = new ListView("model-list-smoke", model, modelStyle,
			modelController, 120.0, -1, function(index) { modelSelection = index; });
		var modelRoot = context.submit(modelList, new LayoutFrame(256.0, 120.0));
		var modelSemantics:Semantics = cast modelRoot.semantics;
		var firstModelRowId = 0;
		modelRoot.walk(function(node) {
			var semantics:Null<Semantics> = node.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.CollectionItem &&
				semantics.positionInSet == 1)
				firstModelRowId = node.id.value;
		});
		var firstModelExtentCalls = model.extentCalls;
		if (modelSemantics.role != AccessibilityRole.Collection || modelSemantics.setSize != 100000 ||
			modelBuiltRows.length == 0 || modelBuiltRows.length > 16 || firstModelExtentCalls != 100000 ||
			modelBuiltRows[0] != 0 || modelController.maxScrollY <= 0.0)
			return 160;
		modelController.jumpTo(0.0, model.offsetBefore(50000));
		modelBuiltRows.resize(0);
		modelRoot = context.submit(modelList, new LayoutFrame(256.0, 120.0));
		var recycledModelRowId = 0;
		modelRoot.walk(function(node) {
			var semantics:Null<Semantics> = node.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.CollectionItem &&
				semantics.positionInSet == 50000)
				recycledModelRowId = node.id.value;
		});
		if (modelBuiltRows.length == 0 || modelBuiltRows.length > 16 || modelBuiltRows[0] != 49999 ||
			model.extentCalls != firstModelExtentCalls || recycledModelRowId != firstModelRowId ||
			modelList.materializedFirst != 49999)
			return 161;
		var incrementalExtentCalls = model.extentCalls;
		model.bumpExtent(50000);
		modelBuiltRows.resize(0);
		modelRoot = context.submit(modelList, new LayoutFrame(256.0, 120.0));
		if (model.extentCalls != incrementalExtentCalls + 1 ||
			modelList.extentMeasurements != incrementalExtentCalls + 1 ||
			modelList.extentReuses < 99999)
			return 174;
		if (!modelList.select(50000) || modelList.selectedIndex != 50000 || modelSelection != 50000)
			return 162;
		modelRoot = context.submit(modelList, new LayoutFrame(256.0, 120.0));
		var selectedModelItem:Null<RenderNode> = null;
		modelRoot.walk(function(node) {
			var nodeSemantics:Null<Semantics> = node.semantics;
			if (nodeSemantics != null && nodeSemantics.role == AccessibilityRole.CollectionItem &&
				nodeSemantics.positionInSet == 50001)
				selectedModelItem = node;
		});
		if (selectedModelItem == null)
			return 163;
		var selectedModelSemantics:Null<Semantics> = selectedModelItem.semantics;
		if (selectedModelSemantics == null ||
			(selectedModelSemantics.states & AccessibilityState.Selected) == 0)
			return 164;
		if (!context.focusWidget(selectedModelItem.id))
			return 166;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		if (modelList.selectedIndex != 50001 || modelSelection != 50001)
			return 167;
		if (!modelList.scrollTo(0) || modelController.offsetY != 0.0)
			return 165;
		var treeBuiltKeys:Array<String> = [];
		var treeSelection:String = "";
		var treeExpansionKey:String = "";
		var treeExpansionValue = false;
		var treeModel = new SmokeTreeModel(treeBuiltKeys);
		var treeController = new ScrollController();
		var treeStyle = new LayoutStyle();
		treeStyle.width = LayoutAxis.fixed(256.0);
		treeStyle.height = LayoutAxis.fixed(120.0);
		var tree = new TreeView("tree-smoke", treeModel, treeStyle, treeController, 120.0,
			null, null, function(nodeKey) { treeSelection = nodeKey; }, null,
			function(nodeKey, expanded) {
				treeExpansionKey = nodeKey;
				treeExpansionValue = expanded;
			});
		var treeRoot = context.submit(tree, new LayoutFrame(256.0, 120.0));
		var treeSemantics:Semantics = cast treeRoot.semantics;
		if (treeSemantics.role != AccessibilityRole.Tree || treeSemantics.setSize <= 100000 ||
			treeBuiltKeys.length == 0 || treeBuiltKeys.length > 16 ||
			treeBuiltKeys[0] != "root:0" || !tree.isExpanded("root:0"))
			return 168;
		if (!tree.select("root:0:child:2") || treeSelection != "root:0:child:2")
			return 169;
		if (!tree.scrollTo("root:0:child:2") || treeController.offsetY <= 0.0)
			return 170;
		if (!tree.setExpanded("root:0", false) || treeExpansionKey != "root:0" || treeExpansionValue)
			return 171;
		tree.scrollTo("root:0");
		treeBuiltKeys.resize(0);
		treeRoot = context.submit(tree, new LayoutFrame(256.0, 120.0));
		if (treeBuiltKeys.length == 0 || treeBuiltKeys[0] != "root:0" ||
			tree.isExpanded("root:0"))
			return 172;
		if (!tree.toggleExpanded("root:0") || !tree.isExpanded("root:0"))
			return 173;
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
		context.scroll(innerGeometry.x + 4.0, innerGeometry.y + 4.0, 0.0, 60.0);
		if (innerController.offsetY != 60.0 || outerController.offsetY != 0.0)
			return 102;
		context.scroll(4.0, 180.0, 0.0, 50.0);
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
		var centeredDialogY = 96.0 - dialogPanelGeometry.height * 0.5;
		if (dialogFocus == null || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(dialogFocus) ||
			dialogPanelGeometry.x < centeredDialogX - 0.1 ||
			dialogPanelGeometry.x > centeredDialogX + 0.1 ||
			dialogPanelGeometry.y < centeredDialogY - 0.1 ||
			dialogPanelGeometry.y > centeredDialogY + 0.1)
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
		var menuGeometry:ResolvedLayoutItem = cast menuRoot.children[1].resolved;
		if (context.focus.focusedId == null ||
			menuRoot.children[0].layout.style.background.alpha != 0.0 ||
			menuGeometry.x != 32.0 || menuGeometry.y != 24.0)
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
		var radioIndicator:ResolvedLayoutItem = cast radioRoot.children[0].children[0].resolved;
		var radioDot:ResolvedLayoutItem = cast radioRoot.children[0].children[0].children[0].resolved;
		if (Math.abs((radioIndicator.x + radioIndicator.width * 0.5) -
			(radioDot.x + radioDot.width * 0.5)) > 0.01 ||
			Math.abs((radioIndicator.y + radioIndicator.height * 0.5) -
			(radioDot.y + radioDot.height * 0.5)) > 0.01)
			return 253;
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
		if (theme.tokens.textPrimary != theme.text || theme.tokens.textSecondary != theme.mutedText ||
			theme.tokens.surface != theme.panelBackground || theme.tokens.focusRing != theme.buttonFocused ||
			theme.tokens.radiusMedium != 6.0 || theme.tokens.spacingMedium != 8.0)
			return 225;
		theme.body.color = Color.rgba(0.10, 0.14, 0.21, 1.0);
		theme.button.color = Color.rgba(1.0, 1.0, 1.0, 1.0);
		var styleTheme = new StyleSheet("StyleTheme");
		styleTheme.rule(StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.1, 0.1, 0.1, 1.0))]);
		styleTheme.rule(StyleSelector.widget("button").state(StyleState.Hovered),
			[StyleValue.background(Color.rgba(0.2, 0.2, 0.2, 1.0))]);
		var styleApplication = new StyleSheet("StyleApplication");
		styleApplication.rule(StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.3, 0.3, 0.3, 1.0))]);
		styleApplication.rule(StyleSelector.widget("button").state(StyleState.Hovered),
			[StyleValue.background(Color.rgba(0.4, 0.4, 0.4, 1.0))]);
		var localStyle = new LayoutStyle();
		localStyle.width = LayoutAxis.fixed(88.0);
		var computed = new StyleResolver().resolve(
			new StyleTarget("button", "style-key", "style-id", ["primary"], ["button"],
				StyleStateUtil.withState(0, StyleState.Hovered, true)),
			null, styleTheme, styleApplication, localStyle);
		var computedSource:Null<StyleSource> = computed.source(StyleProperty.Background);
		var computedSourceStylesheet = computedSource == null ? "" : computedSource.stylesheet;
		var computedSourceSelector = computedSource == null ? "" : computedSource.selector;
		if (computed.get(StyleProperty.Background).red != 0.4 ||
			computed.get(StyleProperty.Width).sizing != LayoutSizing.Fixed ||
			computed.get(StyleProperty.Width).value != 88.0 || computedSource == null ||
			computedSourceStylesheet != "StyleApplication" ||
			computedSourceSelector != "button:hovered")
			return 214;
		var cacheSheet = new StyleSheet("CacheStyles");
		cacheSheet.rule(StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.3, 0.3, 0.3, 1.0)),
				StyleValue.width(LayoutAxis.grow(100.0, 500.0, 3.0))]);
		var cacheResolver = new StyleResolver();
		var cacheTarget = new StyleTarget("button", "cache-key");
		var cachedFirst = cacheResolver.resolve(cacheTarget, null, null, cacheSheet);
		var cachedSecond = cacheResolver.resolve(cacheTarget, null, null, cacheSheet);
		if (cacheResolver.cacheMisses != 1 || cacheResolver.cacheHits != 1 ||
			cacheResolver.cachedStyleCount != 1 || cachedSecond.get(StyleProperty.Background).red != 0.3)
			return 230;
		var cachedAxisLayout = cachedSecond.copy().toLayoutStyle();
		if (cachedAxisLayout.width.sizing != LayoutSizing.Grow || cachedAxisLayout.width.min != 100.0 ||
			cachedAxisLayout.width.max != 500.0 || cachedAxisLayout.width.growWeight != 3.0)
			return 236;
		cachedSecond.get(StyleProperty.Width).value = 123.0;
		cachedFirst.set(StyleProperty.Background, Color.rgba(1.0, 0.0, 0.0, 1.0), null);
		var cachedThird = cacheResolver.resolve(cacheTarget, null, null, cacheSheet);
		if (cacheResolver.cacheHits != 2 || cachedThird.get(StyleProperty.Background).red != 0.3 ||
			cachedThird.get(StyleProperty.Width).value != 0.0)
			return 231;
		var localAxis = new LayoutStyle();
		localAxis.width = LayoutAxis.fit(100.0, 500.0);
		var localAxisResolver = new StyleResolver();
		var localAxisTarget = new StyleTarget("box", "local-axis");
		var localAxisFirst = localAxisResolver.resolve(localAxisTarget, null, null, null, localAxis);
		var localAxisFirstValue = localAxisFirst.get(StyleProperty.Width);
		if (localAxisResolver.cacheMisses != 1 || !localAxisFirst.has(StyleProperty.Width) ||
			localAxisFirstValue.min != 100.0 || localAxisFirstValue.max != 500.0)
			return 237;
		localAxis.width = LayoutAxis.grow(100.0, 500.0, 3.0);
		var localAxisSecond = localAxisResolver.resolve(localAxisTarget, null, null, null, localAxis);
		var localAxisSecondValue = localAxisSecond.get(StyleProperty.Width);
		if (localAxisResolver.cacheMisses != 2 || localAxisResolver.cacheHits != 0 ||
			localAxisSecondValue.sizing != LayoutSizing.Grow || localAxisSecondValue.growWeight != 3.0)
			return 238;
		localAxis.width.growWeight = 4.0;
		var localAxisThird = localAxisResolver.resolve(localAxisTarget, null, null, null, localAxis);
		if (localAxisResolver.cacheMisses != 3 || localAxisThird.get(StyleProperty.Width).growWeight != 4.0)
			return 239;
		var axisBefore = new ComputedStyle();
		axisBefore.set(StyleProperty.Width, LayoutAxis.grow(100.0, 500.0, 3.0), null);
		var axisAfter = new ComputedStyle();
		axisAfter.set(StyleProperty.Width, LayoutAxis.grow(100.0, 500.0, 4.0), null);
		if (!StyleDiff.compare(axisBefore, axisAfter).changed)
			return 240;
		cacheSheet.rule(StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.6, 0.6, 0.6, 1.0))]);
		var revised = cacheResolver.resolve(cacheTarget, null, null, cacheSheet);
		if (cacheResolver.cacheMisses != 2 || revised.get(StyleProperty.Background).red != 0.6)
			return 232;
		cacheTarget = new StyleTarget("button", "cache-key", null, null, null, StyleState.Hovered);
		var stateChanged = cacheResolver.resolve(cacheTarget, null, null, cacheSheet);
		if (cacheResolver.cacheMisses != 3 || stateChanged.get(StyleProperty.Background).red != 0.6)
			return 233;
		var cacheEnvironment = new StyleEnvironment(500.0, 800.0);
		var environmentSheet = new StyleSheet("CacheEnvironment");
		environmentSheet.rule(StyleSelector.widget("button"),
			[StyleValue.paddingSymmetric(12.0, 8.0)]);
		environmentSheet.when(Environment.widthLessThan(600.0), StyleSelector.widget("button"),
			[StyleValue.paddingSymmetric(6.0, 4.0)]);
		var environmentResolver = new StyleResolver();
		environmentResolver.resolve(cacheTarget, null, null, environmentSheet, null, cacheEnvironment);
		environmentResolver.resolve(cacheTarget, null, null, environmentSheet, null, cacheEnvironment);
		cacheEnvironment.setViewport(800.0, 500.0);
		var environmentChanged = environmentResolver.resolve(cacheTarget, null, null,
			environmentSheet, null, cacheEnvironment);
		if (environmentResolver.cacheHits != 1 || environmentResolver.cacheMisses != 2 ||
			environmentChanged.get(StyleProperty.Padding).left != 12.0)
			return 234;
		var replacementEnvironment = new StyleEnvironment(800.0, 500.0);
		var replacementEnvironmentStyle = environmentResolver.resolve(cacheTarget, null, null,
			environmentSheet, null, replacementEnvironment);
		if (environmentResolver.cacheMisses != 3 ||
			replacementEnvironmentStyle.get(StyleProperty.Padding).left != 12.0)
			return 235;
		var inherited = new ComputedStyle();
		inherited.set(StyleProperty.TextColor, Color.rgba(0.7, 0.7, 0.7, 1.0), null);
		var inheritedChild = new StyleResolver().resolve(new StyleTarget("label"), inherited);
		if (inheritedChild.get(StyleProperty.TextColor).red != 0.7)
			return 215;
		var transitionSheet = new StyleSheet("TransitionSheet");
		transitionSheet.rule(StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.0, 0.0, 0.0, 1.0))]);
		transitionSheet.rule(StyleSelector.widget("button").state(StyleState.Hovered),
			[StyleValue.background(Color.rgba(1.0, 1.0, 1.0, 1.0))]);
		transitionSheet.transition(StyleProperty.Background, 0.1, Easing.Linear);
		var transitionScheduler = new AnimationScheduler();
		var transitionResolver = new StyleResolver(transitionScheduler);
		var transitionTarget = new StyleTarget("button", "transition-key", "transition-id",
			null, null, 0);
		var normalComputed = transitionResolver.resolve(transitionTarget, null, transitionSheet);
		transitionTarget = new StyleTarget("button", "transition-key", "transition-id",
			null, null, StyleState.Hovered);
		var animatedComputed = transitionResolver.resolve(transitionTarget, null, transitionSheet);
		if (animatedComputed.get(StyleProperty.Background).red != 0.0 ||
			transitionScheduler.activeCount != 1)
			return 216;
		transitionScheduler.advance(0.05);
		animatedComputed = transitionResolver.resolve(transitionTarget, null, transitionSheet);
		if (animatedComputed.get(StyleProperty.Background).red <= 0.0 ||
			animatedComputed.get(StyleProperty.Background).red >= 1.0)
			return 217;
		transitionScheduler.advance(0.05);
		animatedComputed = transitionResolver.resolve(transitionTarget, null, transitionSheet);
		if (animatedComputed.get(StyleProperty.Background).red != 1.0 ||
			transitionScheduler.activeCount != 0 || normalComputed.get(StyleProperty.Background).red != 0.0 ||
			transitionResolver.cacheHits != 0 || transitionResolver.cacheMisses != 0)
			return 218;
		var effectChain = EffectChain.of([
			BlurEffect.withSigma(12.0), new SaturateEffect(1.15)
		]);
		var effectOverflow = effectChain.inkOverflow();
		if (effectChain.effects.length != 2 || effectOverflow.left != 36.0 ||
			effectOverflow.top != 36.0 || effectOverflow.right != 36.0 ||
			effectOverflow.bottom != 36.0 || !effectChain.isEqual(effectChain.copy()))
			return 240;
		var normalizedEffects = EffectChain.of([
			new BrightnessEffect(1.2), new ContrastEffect(0.8), BlurEffect.withSigma(3.0),
			new SaturateEffect(1.1)
		]).normalized();
		if (normalizedEffects.effects.length != 3 ||
			normalizedEffects.effects[0].kind != EffectKind.ColorMatrix ||
			normalizedEffects.effects[1].kind != EffectKind.Blur ||
			normalizedEffects.effects[2].kind != EffectKind.ColorMatrix)
			return 242;
		var blurThenMatrix = EffectChain.of([
			BlurEffect.withSigma(2.0), new BrightnessEffect(1.2)
		]).normalized();
		var multipleBlurs = EffectChain.of([
			BlurEffect.withSigma(2.0), BlurEffect.withSigma(4.0)
		]).normalized();
		if (blurThenMatrix.effects.length != 2 ||
			blurThenMatrix.effects[0].kind != EffectKind.Blur ||
			blurThenMatrix.effects[1].kind != EffectKind.ColorMatrix ||
			multipleBlurs.effects.length != 2)
			return 246;
		var customDefinition = new CustomEffectDefinition(7, "wave", [
			EffectParameterType.Float, EffectParameterType.Color
		], new InkOverflow(2.0, 3.0, 4.0, 5.0));
		var custom = new CustomEffect(customDefinition, [
			EffectParameter.scalar(0.25),
			EffectParameter.color(Color.rgba(0.1, 0.2, 0.3, 0.4))
		]);
		var customCopy:CustomEffect = cast custom.copy();
		var customMid:CustomEffect = cast custom.interpolate(new CustomEffect(customDefinition, [
			EffectParameter.scalar(0.75),
			EffectParameter.color(Color.rgba(0.5, 0.6, 0.7, 0.8))
		]), 0.5);
		if (custom.components.length != 5 || custom.components[0] != 0.25 ||
			custom.components[4] != 0.4 || !custom.isEqual(customCopy) ||
			custom.inkOverflow().right != 4.0 || Math.abs(customMid.components[0] - 0.5) > 0.00001 ||
			Math.abs(customMid.components[1] - 0.3) > 0.00001)
			return 250;
		var customRuntime = new CustomEffect(new CustomEffectDefinition(8, "multiply", [
			EffectParameterType.Float
		], new InkOverflow(1.0, 1.0, 1.0, 1.0)), [EffectParameter.scalar(0.75)]);
		var mixedCustom = EffectChain.of([
			new BrightnessEffect(1.2), customRuntime, BlurEffect.withSigma(3.0)
		]).normalized();
		if (mixedCustom.effects.length != 3 ||
			mixedCustom.effects[0].kind != EffectKind.ColorMatrix ||
			mixedCustom.effects[1].kind != EffectKind.Custom ||
			mixedCustom.effects[2].kind != EffectKind.Blur)
			return 246;
		var interpolatedEffects = EffectChain.interpolate(
			EffectChain.of([BlurEffect.withSigma(4.0), new BrightnessEffect(1.0)]),
			EffectChain.of([BlurEffect.withSigma(12.0), new BrightnessEffect(2.0)]), 0.5);
		var interpolatedBlur:BlurEffect = cast interpolatedEffects.effects[0];
		var interpolatedBrightness:BrightnessEffect = cast interpolatedEffects.effects[1];
		if (interpolatedBlur == null || interpolatedBlur.sigma != 8.0 ||
			interpolatedBrightness == null || interpolatedBrightness.factor != 1.5)
			return 241;
		var effectTransitionSheet = new StyleSheet("EffectTransitionSheet");
		effectTransitionSheet.rule(StyleSelector.widget("panel"),
			[StyleValue.effects(EffectChain.of([BlurEffect.withSigma(4.0)]))]);
		effectTransitionSheet.rule(StyleSelector.widget("panel").state(StyleState.Hovered),
			[StyleValue.effects(EffectChain.of([BlurEffect.withSigma(12.0)]))]);
		effectTransitionSheet.transition(StyleProperty.Effects, 0.1, Easing.Linear);
		var effectTransitionScheduler = new AnimationScheduler();
		var effectTransitionResolver = new StyleResolver(effectTransitionScheduler);
		var effectTransitionTarget = new StyleTarget("panel", "effect-transition-key", "effect-transition-id",
			null, null, 0);
		effectTransitionResolver.resolve(effectTransitionTarget, null, effectTransitionSheet);
		effectTransitionTarget = new StyleTarget("panel", "effect-transition-key", "effect-transition-id",
			null, null, StyleState.Hovered);
		effectTransitionResolver.resolve(effectTransitionTarget, null, effectTransitionSheet);
		if (effectTransitionScheduler.activeCount != 1)
			return 245;
		effectTransitionScheduler.advance(0.05);
		var effectTransitionComputed = effectTransitionResolver.resolve(effectTransitionTarget, null,
			effectTransitionSheet);
		var effectTransitionBlur:BlurEffect = cast effectTransitionComputed.get(StyleProperty.Effects).effects[0];
		if (effectTransitionBlur == null || effectTransitionBlur.sigma != 8.0)
			return 246;
		var dropShadow = new DropShadowEffect(0.0, 6.0, 12.0,
			Color.rgba(0.0, 0.0, 0.0, 0.35));
		var matrixDropMatrix = EffectChain.of([
			new BrightnessEffect(1.2), dropShadow, new ContrastEffect(0.8)
		]).normalized();
		if (matrixDropMatrix.effects.length != 3 ||
			matrixDropMatrix.effects[0].kind != EffectKind.ColorMatrix ||
			matrixDropMatrix.effects[1].kind != EffectKind.DropShadow ||
			matrixDropMatrix.effects[2].kind != EffectKind.ColorMatrix)
			return 246;
		var dropOverflow = dropShadow.inkOverflow();
		if (dropOverflow.left != 36.0 || dropOverflow.top != 30.0 ||
			dropOverflow.right != 36.0 || dropOverflow.bottom != 42.0)
			return 242;
		var effectSheet = new StyleSheet("EffectSheet");
		effectSheet.rule(StyleSelector.widget("panel"), [
			StyleValue.effects(effectChain),
			StyleValue.backdropEffects(EffectChain.of([
				BlurEffect.withSigma(8.0), new ContrastEffect(1.1), HueRotateEffect.withDegrees(12.0),
				ColorMatrixEffect.identity(), dropShadow
			]))
		]);
		var effectTarget = new StyleTarget("panel", "effect-key");
		var effectComputed = new StyleResolver().resolve(effectTarget, null, null, effectSheet);
		var effectsSource = effectComputed.source(StyleProperty.Effects);
		var backdropSource = effectComputed.source(StyleProperty.BackdropEffects);
		if (!effectComputed.get(StyleProperty.Effects).isEqual(effectChain) ||
			effectsSource == null || effectsSource.stylesheet != "EffectSheet" ||
			backdropSource == null || !effectComputed.has(StyleProperty.BackdropEffects))
			return 243;
		var emptyEffects = new ComputedStyle();
		emptyEffects.set(StyleProperty.Effects, EffectChain.empty(), null);
		var changedEffects = new ComputedStyle();
		changedEffects.set(StyleProperty.Effects, effectChain, null);
		var effectsDiff = StyleDiff.compare(emptyEffects, changedEffects);
		var effectDescription = "";
		for (entry in effectComputed.entries())
			if (entry.name == "effects")
				effectDescription = entry.describe();
		if (!effectsDiff.changed || effectsDiff.impact != StyleImpact.Composite ||
			effectComputed.entries().length < StyleProperty.all().length ||
			effectDescription.indexOf("[blur(12") != 0)
			return 244;
		var decorationContractResult = decorationStyleContract();
		if (decorationContractResult != 0)
			return decorationContractResult;
		var colorAdjustments = EffectChain.of([
			new BrightnessEffect(2.0), new ContrastEffect(0.5)
		]);
		var colorMatrix = colorAdjustments.colorMatrix();
		if (colorMatrix.length != ColorMatrixEffect.ComponentCount ||
			colorMatrix[0] != 1.0 || colorMatrix[6] != 1.0 ||
			colorMatrix[12] != 1.0 || colorMatrix[18] != 1.0 ||
			colorMatrix[4] != 0.25 || colorMatrix[9] != 0.25 ||
			colorMatrix[14] != 0.25)
			return 247;
		var effectCanvas = new Canvas();
		var effectList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0), colorAdjustments);
		effectCanvas.update(effectList);
		if (effectList.info().commandCount != 4)
			return 248;
		effectList.dispose();
		effectCanvas.reset();
		var boundedEmptyList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(_) {}, CompositeMode.SourceOver,
			new Rect(4.0, 6.0, 24.0, 18.0));
		effectCanvas.update(boundedEmptyList);
		var boundedEmptyInfo = boundedEmptyList.info();
		if (boundedEmptyInfo.commandCount != 2 || boundedEmptyInfo.commandBytes >= 3220)
			return 256;
		boundedEmptyList.dispose();
		effectCanvas.reset();
		var customList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0),
			EffectChain.of([customRuntime]));
		effectCanvas.update(customList);
		if (customList.info().commandCount != 4)
			return 251;
		customList.dispose();
		effectCanvas.reset();
		var blurList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0),
			EffectChain.of([BlurEffect.withSigma(4.0)]));
		effectCanvas.update(blurList);
		if (blurList.info().commandCount != 4)
			return 249;
		blurList.dispose();
		effectCanvas.reset();
		var dropShadowList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0),
			EffectChain.of([dropShadow]));
		effectCanvas.update(dropShadowList);
		if (dropShadowList.info().commandCount != 4)
			return 250;
		dropShadowList.dispose();
		effectCanvas.reset();
		var roundedMask = Mask.roundedRect(10.0);
		if (!roundedMask.isEqual(roundedMask.copy()))
			return 251;
		var maskSheet = new StyleSheet("MaskSheet");
		maskSheet.rule(StyleSelector.widget("panel"), [StyleValue.mask(roundedMask)]);
		var noMaskParent:Null<ComputedStyle> = null;
		var noMaskTheme:Null<StyleSheet> = null;
		var maskComputed = new StyleResolver().resolve(new StyleTarget("panel", "mask-key"),
			noMaskParent, noMaskTheme, maskSheet);
		var maskSource = maskComputed.source(StyleProperty.Mask);
		if (maskComputed.get(StyleProperty.Mask) == null ||
			!maskComputed.get(StyleProperty.Mask).isEqual(roundedMask) || maskSource == null ||
			maskSource.stylesheet != "MaskSheet")
			return 252;
		var emptyMask = new ComputedStyle();
		var noMask:Mask = null;
		var noMaskSource:Null<StyleSource> = null;
		emptyMask.set(StyleProperty.Mask, noMask, noMaskSource);
		var changedMask = new ComputedStyle();
		var typedMask:Mask = roundedMask;
		changedMask.set(StyleProperty.Mask, typedMask, noMaskSource);
		var maskDiff = StyleDiff.compare(emptyMask, changedMask);
		if (!maskDiff.changed || maskDiff.impact != StyleImpact.Composite)
			return 253;
		var maskList = DisplayList.create();
		var noMaskEffects:EffectChain = null;
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0),
			noMaskEffects, roundedMask);
		effectCanvas.update(maskList);
		if (maskList.info().commandCount != 4)
			return 254;
		maskList.dispose();
		effectCanvas.reset();
		var backdropList = DisplayList.create();
		effectCanvas.withLayer(1.0, function(canvas) {
			canvas.fillRect(new Rect(4.0, 6.0, 24.0, 18.0), Color.rgba(0.2, 0.4, 0.8, 1.0));
		}, CompositeMode.SourceOver, new Rect(4.0, 6.0, 24.0, 18.0), null, null,
			EffectChain.of([new BrightnessEffect(1.1)]));
		effectCanvas.update(backdropList);
		if (backdropList.info().commandCount != 4)
			return 255;
		backdropList.dispose();
		effectCanvas.reset();
		var eightOperations = EffectChain.of([
			BlurEffect.withSigma(1.0), BlurEffect.withSigma(2.0), BlurEffect.withSigma(3.0),
			BlurEffect.withSigma(4.0), BlurEffect.withSigma(5.0), BlurEffect.withSigma(6.0),
			BlurEffect.withSigma(7.0), BlurEffect.withSigma(8.0)
		]);
		var eightOperationCanvas = new Canvas();
		var eightOperationList = DisplayList.create();
		eightOperationCanvas.beginLayer(1.0, CompositeMode.SourceOver, null, eightOperations);
		eightOperationCanvas.endLayer();
		eightOperationCanvas.update(eightOperationList);
		eightOperationList.dispose();
		var rejectedNineOperations = false;
		try {
			new Canvas().beginLayer(1.0, CompositeMode.SourceOver, null, EffectChain.of([
				BlurEffect.withSigma(1.0), BlurEffect.withSigma(2.0), BlurEffect.withSigma(3.0),
				BlurEffect.withSigma(4.0), BlurEffect.withSigma(5.0), BlurEffect.withSigma(6.0),
				BlurEffect.withSigma(7.0), BlurEffect.withSigma(8.0), BlurEffect.withSigma(9.0)
			]));
		} catch (_:Dynamic) {
			rejectedNineOperations = true;
		}
		if (!rejectedNineOperations)
			return 257;
		var responsiveSheet = new StyleSheet("ResponsiveSheet");
		responsiveSheet.rule(StyleSelector.widget("button"),
			[StyleValue.paddingSymmetric(12.0, 8.0)]);
		responsiveSheet.when(Environment.widthLessThan(600.0), StyleSelector.widget("button"),
			[StyleValue.paddingSymmetric(6.0, 4.0)]);
		responsiveSheet.when(Environment.colorScheme(EnvironmentColorScheme.Dark),
			StyleSelector.widget("button"),
			[StyleValue.background(Color.rgba(0.05, 0.05, 0.05, 1.0))]);
		var responsiveEnvironment = new StyleEnvironment(500.0, 800.0);
		var responsive = new StyleResolver().resolve(transitionTarget, null, null,
			responsiveSheet, null, responsiveEnvironment);
		if (responsive.get(StyleProperty.Padding).left != 6.0)
			return 219;
		responsiveEnvironment.setViewport(800.0, 500.0);
		responsiveEnvironment.colorScheme = EnvironmentColorScheme.Dark;
		responsive = new StyleResolver().resolve(transitionTarget, null, null,
			responsiveSheet, null, responsiveEnvironment);
		if (responsive.get(StyleProperty.Padding).left != 12.0 ||
			responsive.get(StyleProperty.Background).red != 0.05)
			return 220;
		var inheritanceSheet = new StyleSheet("InheritanceSheet");
		inheritanceSheet.rule(StyleSelector.key("inherit-parent"), [
			StyleValue.textColor(Color.rgba(0.75, 0.25, 0.15, 1.0)), StyleValue.fontSize(22.0)]);
		context.setStyleSheet(inheritanceSheet);
		var inheritanceRoot = context.submit(new Column("inherit-parent", [
			new KeyedView("inherited-label", new Text("Inherited"))
		]), new LayoutFrame(256.0, 192.0));
		var inheritedTextNode = inheritanceRoot.children[0];
		var inheritedTextSource:Null<StyleSource> = inheritedTextNode.computedStyle == null ? null :
			inheritedTextNode.computedStyle.source(StyleProperty.TextColor);
		if (inheritedTextNode.computedStyle == null ||
			inheritedTextNode.computedStyle.get(StyleProperty.TextColor).red != 0.75 ||
			inheritedTextNode.computedStyle.get(StyleProperty.FontSize) != 22.0 ||
			inheritedTextSource == null || inheritedTextSource.stylesheet != "InheritanceSheet")
			return 224;
		context.setStyleSheet(new StyleSheet("Application"));
		var lightNeutral = Color.rgba(0.87, 0.90, 0.95, 1.0);
		var accentButton = Color.rgba(0.18, 0.39, 0.70, 1.0);
		if (theme.buttonLabelColor(true, lightNeutral) != theme.body.color ||
			theme.buttonLabelColor(true, accentButton) != theme.button.color ||
			theme.buttonLabelColor(false, accentButton) != theme.disabledButtonText)
			return 101;
		theme.buttonHover = Color.rgba(0.8, 0.1, 0.1, 1.0);
		theme.buttonPressed = Color.rgba(0.7, 0.05, 0.05, 1.0);
		theme.buttonFocused = Color.rgba(0.4, 0.2, 0.8, 1.0);
		theme.buttonDisabled = Color.rgba(0.2, 0.2, 0.2, 1.0);
		theme.body.textStyle = new TextStyle(17.0, FontFamily.Default, -0.25);
		theme.body.paragraphStyle = new ParagraphStyle(TextWrap.Word, TextAlignment.Start, 21.0,
			TextDirection.Ltr);
		theme.heading.textStyle = new TextStyle(26.0);
		theme.label.textStyle = new TextStyle(13.0);
		theme.label.color = Color.rgba(0.32, 0.58, 0.76, 1.0);
		theme.caption.textStyle = new TextStyle(11.0);
		theme.button.textStyle = new TextStyle(15.0);
		context.setTheme(theme);
		if (!UiDirtyFlag.contains(context.dirtyFlags, UiDirtyFlag.NeedsStyle) ||
			!UiDirtyFlag.contains(context.dirtyFlags, UiDirtyFlag.NeedsPaint))
			return 222;
		var themedTextRoot = context.submit(new Text("Theme typography"),
			new LayoutFrame(256.0, 192.0));
		if (themedTextRoot.layout.textStyle.fontSize != 17.0 ||
			themedTextRoot.layout.textStyle.letterSpacing != -0.25 ||
			themedTextRoot.layout.paragraphStyle.wrap != TextWrap.Word ||
			themedTextRoot.layout.paragraphStyle.lineHeight != 21.0 ||
			themedTextRoot.layout.paragraphStyle.direction != TextDirection.Ltr ||
			themedTextRoot.layout.textColor != theme.body.color)
			return 214;
		var letterSpacingSheet = new StyleSheet("LetterSpacingInvalidation");
		letterSpacingSheet.rule(StyleSelector.widget("text"), [StyleValue.letterSpacing(0.75)]);
		context.setStyleSheet(letterSpacingSheet);
		context.submit(new Text("Dirty propagation"), new LayoutFrame(256.0, 192.0));
		var letterSpacingMetrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (letterSpacingMetrics == null || letterSpacingMetrics.textLayoutInvalidatedNodes <= 0 ||
			letterSpacingMetrics.layoutInvalidatedNodes <= 0 ||
			letterSpacingMetrics.paintInvalidatedNodes <= 0 ||
			letterSpacingMetrics.hitGeometryInvalidatedNodes <= 0 ||
			!UiDirtyFlag.contains(letterSpacingMetrics.styleInvalidationFlags,
				UiDirtyFlag.NeedsTextLayout) ||
			!UiDirtyFlag.contains(letterSpacingMetrics.styleInvalidationFlags,
				UiDirtyFlag.NeedsLayout) ||
			!UiDirtyFlag.contains(letterSpacingMetrics.styleInvalidationFlags,
				UiDirtyFlag.NeedsPaint) ||
			!UiDirtyFlag.contains(letterSpacingMetrics.styleInvalidationFlags,
				UiDirtyFlag.NeedsHitGeometry))
			return 225;
		context.setStyleSheet(new StyleSheet("Application"));
		var roleRoot = context.submit(new Column("text-roles", [
			new KeyedView("body", new Text("Body")),
			new KeyedView("heading", new Text("Heading", null, null, null, TextRole.Heading)),
			new KeyedView("label", new Text("Label", null, null, null, TextRole.Label)),
			new KeyedView("caption", new Text("Caption", null, null, null, TextRole.Caption)),
			new KeyedView("button", new Text("Button", null, null, null, TextRole.Button))
		]), new LayoutFrame(320.0, 320.0));
		if (roleRoot.children[0].layout.textStyle.fontSize != 17.0)
			return 218;
		if (roleRoot.children[1].layout.textStyle.fontSize != 26.0)
			return 219;
		if (roleRoot.children[2].layout.textStyle.fontSize != 13.0 ||
			roleRoot.children[2].layout.textColor != theme.label.color)
			return 220;
		if (roleRoot.children[2].layout.paragraphStyle.wrap != TextWrap.None)
			return 223;
		if (roleRoot.children[3].layout.textStyle.fontSize != 11.0 ||
			roleRoot.children[3].layout.textColor != theme.caption.color)
			return 221;
		if (roleRoot.children[4].layout.textStyle.fontSize != 15.0 ||
			roleRoot.children[4].layout.paragraphStyle.wrap != TextWrap.None)
			return 222;
		var appShell = new AppShell("app-shell-smoke", new Text("Content"),
			new Text("Top bar"), new Text("Sidebar"), new Text("Inspector"));
		var appShellRoot = context.submit(appShell, new LayoutFrame(320.0, 192.0));
		if (appShellRoot.children.length != 2 ||
			appShellRoot.children[0].layout.visualKind != LayoutVisualKind.Text ||
			appShellRoot.children[1].layout.style.direction != LayoutDirection.LeftToRight ||
			appShellRoot.children[1].children.length != 3)
			return 224;
		var appShellBody = appShellRoot.children[1];
		var appShellContentSlot = appShellBody.children[1];
		if (appShellBody.children[0].layout.visualKind != LayoutVisualKind.Text ||
			appShellContentSlot.children.length != 1 ||
			appShellContentSlot.children[0].layout.visualKind != LayoutVisualKind.Text ||
			appShellBody.children[2].layout.visualKind != LayoutVisualKind.Text ||
			appShellContentSlot.resolved == null ||
			appShellContentSlot.resolved.width <= 0.0 ||
			appShellContentSlot.resolved.height <= 0.0)
			return 225;
		for (mask in 0...8) {
			var hasTopBar = (mask & 1) != 0;
			var hasSidebar = (mask & 2) != 0;
			var hasInspector = (mask & 4) != 0;
			var permutation = new AppShell('app-shell-permutation-$mask', new Text("Content"),
				hasTopBar ? new Text("Top bar") : null,
				hasSidebar ? new Text("Sidebar") : null,
				hasInspector ? new Text("Inspector") : null);
			var permutationRoot = context.submit(permutation, new LayoutFrame(320.0, 192.0));
			var bodyIndex = hasTopBar ? 1 : 0;
			var expectedBodyChildren = 1 + (hasSidebar ? 1 : 0) + (hasInspector ? 1 : 0);
			if (permutationRoot.children.length != (hasTopBar ? 2 : 1) ||
				permutationRoot.children[bodyIndex].children.length != expectedBodyChildren)
				return 226;
			var permutationBody = permutationRoot.children[bodyIndex];
			var contentIndex = hasSidebar ? 1 : 0;
			var permutationContent = permutationBody.children[contentIndex];
			if (permutationContent.children.length != 1 || permutationContent.resolved == null ||
				permutationContent.resolved.width <= 0.0 || permutationContent.resolved.height <= 0.0)
				return 227;
			var rebuiltRoot = context.submit(permutation, new LayoutFrame(320.0, 192.0));
			var rebuiltBody = rebuiltRoot.children[bodyIndex];
			if (!permutationRoot.id.equals(rebuiltRoot.id) ||
				!permutationContent.id.equals(rebuiltBody.children[contentIndex].id))
				return 228;
		}
		var sourceStyle = new LayoutStyle();
		sourceStyle.width = LayoutAxis.fixed(280.0);
		sourceStyle.height = LayoutAxis.fixed(120.0);
		sourceStyle.padding = new Insets(11.0, 12.0, 13.0, 14.0);
		var sourceBodyStyle = new LayoutStyle();
		sourceBodyStyle.direction = LayoutDirection.TopToBottom;
		sourceBodyStyle.childGap = 17.0;
		var copiedAppShell = new AppShell("app-shell-style-copy", new Text("Content"),
			null, null, null, sourceStyle, sourceBodyStyle);
		sourceStyle.width.value = 1.0;
		sourceStyle.padding.left = 1.0;
		sourceBodyStyle.childGap = 1.0;
		if (copiedAppShell.style.width.value != 280.0 ||
			copiedAppShell.style.padding.left != 11.0 ||
			copiedAppShell.bodyStyle.childGap != 17.0 ||
			copiedAppShell.bodyStyle.direction != LayoutDirection.LeftToRight ||
			sourceBodyStyle.direction != LayoutDirection.TopToBottom)
			return 229;
		var resizedExtent = 0.0;
		var splitOptions = new SplitViewOptions();
		splitOptions.resizableSide = SplitSide.Trailing;
		splitOptions.extent = 96.0;
		splitOptions.minimumExtent = 64.0;
		splitOptions.maximumExtent = 144.0;
		splitOptions.onResize = function(value) { resizedExtent = value; };
		var split = new SplitView("split-smoke", new Text("Primary"),
			new Text("Secondary"), splitOptions);
		var splitRoot = context.submit(split, new LayoutFrame(320.0, 192.0));
		if (splitRoot.children.length != 3 ||
			splitRoot.children[0].children.length != 1 ||
			splitRoot.children[2].children.length != 1)
			return 230;
		var splitDividerSemantics:Semantics = cast splitRoot.children[1].semantics;
		if (splitDividerSemantics == null ||
			splitDividerSemantics.role != AccessibilityRole.Separator ||
			!splitRoot.children[1].focusable ||
			(splitDividerSemantics.actions & AccessibilityAction.Increment) == 0 ||
			(splitDividerSemantics.actions & AccessibilityAction.Decrement) == 0)
			return 230;
		var splitDividerGeometry:ResolvedLayoutItem = cast splitRoot.children[1].resolved;
		var splitPointerX = splitDividerGeometry.x + splitDividerGeometry.width * 0.5;
		var splitPointerY = splitDividerGeometry.y + splitDividerGeometry.height * 0.5;
		context.pointerMove(splitPointerX, splitPointerY);
		if (context.events.cursorShape() != UiCursorShape.HorizontalResize)
			return 236;
		context.pointerDown(splitPointerX, splitPointerY, 0);
		// The native showcase rebuilds its view tree between input events. The
		// divider must retain the active drag across that frame boundary.
		splitRoot = context.submit(split, new LayoutFrame(320.0, 192.0));
		context.pointerMove(splitPointerX + 20.0, splitPointerY);
		if (context.events.cursorShape() != UiCursorShape.HorizontalResize)
			return 237;
		context.pointerUp(splitPointerX + 20.0, splitPointerY, 0);
		if (resizedExtent != 76.0 || split.extent != 76.0)
			return 231;
		context.pointerMove(splitPointerX, splitPointerY);
		context.pointerDown(splitPointerX, splitPointerY, 0);
		context.pointerCancel(0, splitPointerX + 40.0, splitPointerY);
		if (context.events.cursorShape() != UiCursorShape.Arrow)
			return 238;
		split.collapsed = true;
		splitRoot = context.submit(split, new LayoutFrame(320.0, 192.0));
		var collapsedSecondaryGeometry:ResolvedLayoutItem = cast splitRoot.children[2].resolved;
		if (splitRoot.children[2].layout.style.visible ||
			collapsedSecondaryGeometry.width != 0.0)
			return 232;
		split.collapsed = false;
		var expandedSplitRoot = context.submit(split, new LayoutFrame(320.0, 192.0));
		if (!expandedSplitRoot.children[2].layout.style.visible ||
			!splitRoot.children[2].id.equals(expandedSplitRoot.children[2].id))
			return 233;
		var verticalOptions = new SplitViewOptions();
		verticalOptions.orientation = SplitOrientation.Vertical;
		verticalOptions.resizableSide = SplitSide.Trailing;
		verticalOptions.extent = 72.0;
		verticalOptions.minimumExtent = 48.0;
		verticalOptions.maximumExtent = 120.0;
		verticalOptions.onResize = function(value) { resizedExtent = value; };
		var verticalSplit = new SplitView("vertical-split-smoke", new Text("Top"),
			new Text("Bottom"), verticalOptions);
		var verticalRoot = context.submit(verticalSplit, new LayoutFrame(320.0, 192.0));
		if (verticalRoot.children.length != 3 ||
			verticalRoot.layout.style.direction != LayoutDirection.TopToBottom)
			return 234;
		var verticalDividerGeometry:ResolvedLayoutItem = cast verticalRoot.children[1].resolved;
		var verticalPointerX = verticalDividerGeometry.x + verticalDividerGeometry.width * 0.5;
		var verticalPointerY = verticalDividerGeometry.y + verticalDividerGeometry.height * 0.5;
		context.pointerMove(verticalPointerX, verticalPointerY);
		if (context.events.cursorShape() != UiCursorShape.VerticalResize)
			return 239;
		context.pointerDown(verticalPointerX, verticalPointerY, 0);
		context.pointerMove(verticalPointerX, verticalPointerY + 20.0);
		if (context.events.cursorShape() != UiCursorShape.VerticalResize)
			return 240;
		context.pointerUp(verticalPointerX, verticalPointerY + 20.0, 0);
		if (resizedExtent != 52.0 || verticalSplit.extent != 52.0)
			return 235;
		var captureClicks = 0;
		var captureOptions = new SplitViewOptions();
		captureOptions.resizableSide = SplitSide.Trailing;
		captureOptions.extent = 120.0;
		var captureSplit = new SplitView("pointer-capture-split", new Text("Leading"),
			new Button("Drag target", null, function() { captureClicks++; }), captureOptions);
		var captureRoot = context.submit(captureSplit, new LayoutFrame(320.0, 192.0));
		var platformCaptureStates:Array<Bool> = [];
		context.setPointerCaptureHandler(function(captured) platformCaptureStates.push(captured));
		var captureDivider = captureRoot.children[1];
		var captureButton = captureRoot.children[2].children[0];
		var captureHoverEnters = 0;
		var capturePointerDowns = 0;
		captureButton.on(UiEventKind.HoverEnter, function(_) { captureHoverEnters++; });
		captureButton.on(UiEventKind.PointerDown, function(_) { capturePointerDowns++; });
		var captureDividerGeometry:ResolvedLayoutItem = cast captureDivider.resolved;
		var captureButtonGeometry:ResolvedLayoutItem = cast captureButton.resolved;
		var captureDividerX = captureDividerGeometry.x + captureDividerGeometry.width * 0.5;
		var captureDividerY = captureDividerGeometry.y + captureDividerGeometry.height * 0.5;
		var captureButtonX = captureButtonGeometry.x + 2.0;
		var captureButtonY = captureButtonGeometry.y + 2.0;
		context.pointerMove(captureDividerX, captureDividerY);
		context.pointerDown(captureDividerX, captureDividerY, 0);
		if (!context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 1 ||
			!platformCaptureStates[0])
			return 248;
		// Physical capture is shared by the window, while logical capture stays
		// independent per pointer. Releasing pointer 1 must not release pointer 0.
		context.pointerDown(captureDividerX, captureDividerY, 0, 0, 1);
		if (!context.events.hasPointerCapture(captureDivider.id, 1) ||
			platformCaptureStates.length != 1)
			return 253;
		context.pointerUp(captureDividerX, captureDividerY, 0, 0, 1);
		if (context.events.hasPointerCapture(captureDivider.id, 1) ||
			!context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 1)
			return 254;
		context.pointerMove(captureButtonX, captureButtonY);
		if (captureHoverEnters != 0 || capturePointerDowns != 0 || captureClicks != 0 ||
			context.events.hoveredId() == null ||
			!context.events.hoveredId().equals(captureDivider.id) ||
			context.events.cursorShape() != UiCursorShape.HorizontalResize)
			return 249;
		context.pointerUp(captureButtonX, captureButtonY, 0);
		if (captureHoverEnters != 1 || captureClicks != 0 ||
			context.events.hasPointerCapture(captureDivider.id) ||
			platformCaptureStates.length != 2 || platformCaptureStates[1] ||
			context.events.hoveredId() == null ||
			!context.events.hoveredId().equals(captureButton.id) ||
			context.events.cursorShape() != UiCursorShape.Arrow)
			return 250;
		context.pointerMove(captureDividerX, captureDividerY);
		context.pointerDown(captureDividerX, captureDividerY, 0);
		context.pointerMove(captureButtonX, captureButtonY);
		context.pointerCancel(0, captureButtonX, captureButtonY);
		if (context.events.hasPointerCapture(captureDivider.id) ||
			platformCaptureStates.length != 4 || !platformCaptureStates[2] || platformCaptureStates[3] ||
			context.events.hoveredId() != null ||
			context.events.cursorShape() != UiCursorShape.Arrow)
			return 251;
		// Detaching the host bridge releases the OS capture but preserves the
		// logical owner; reattaching must immediately restore physical capture.
		context.pointerMove(captureDividerX, captureDividerY);
		context.pointerDown(captureDividerX, captureDividerY, 0);
		if (!context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 5)
			return 255;
		context.setPointerCaptureHandler(null);
		if (!context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 6 ||
			platformCaptureStates[5])
			return 256;
		context.setPointerCaptureHandler(function(captured) platformCaptureStates.push(captured));
		if (!context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 7 ||
			!platformCaptureStates[6])
			return 257;
		context.windowFocusLost();
		if (context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 8 ||
			platformCaptureStates[7] || context.events.hoveredId() != null)
			return 258;
		context.pointerMove(captureDividerX, captureDividerY);
		context.pointerDown(captureDividerX, captureDividerY, 0);
		context.submit(new Text("Unmounted capture owner"), new LayoutFrame(320.0, 192.0));
		if (context.events.hasPointerCapture(captureDivider.id) || platformCaptureStates.length != 10 ||
			!platformCaptureStates[8] || platformCaptureStates[9])
			return 252;
		context.setPointerCaptureHandler(null);
		if (!checkSplitKeyboard(context))
			return 247;
		var inheritedColor = Color.rgba(0.24, 0.31, 0.42, 1.0);
		var nestedColor = Color.rgba(0.76, 0.42, 0.18, 1.0);
		var typography = new DefaultTextStyle(new Column("typography", [
			new KeyedView("outer", new Text("Outer")),
			new KeyedView("nested", new DefaultTextStyle(new Text("Nested"),
				TextStyleOverride.text(28.0, null, nestedColor))),
			new KeyedView("after", new Text("After"))
		]), TextStyleOverride.combine(
			TextStyleOverride.text(22.0, 1.25, inheritedColor),
			TextStyleOverride.paragraph(null, TextAlignment.Center, 30.0, TextDirection.Rtl)));
		var typographyRoot = context.submit(typography, new LayoutFrame(320.0, 192.0));
		var outerText = typographyRoot.children[0];
		var nestedText = typographyRoot.children[1];
		var afterText = typographyRoot.children[2];
		if (outerText.layout.textStyle.fontSize != 22.0 ||
			outerText.layout.textStyle.letterSpacing != 1.25 ||
			outerText.layout.paragraphStyle.alignment != TextAlignment.Center ||
			outerText.layout.paragraphStyle.lineHeight != 30.0 ||
			outerText.layout.paragraphStyle.direction != TextDirection.Rtl ||
			outerText.layout.textColor != inheritedColor ||
			nestedText.layout.textStyle.fontSize != 28.0 ||
			nestedText.layout.textStyle.letterSpacing != 1.25 ||
			nestedText.layout.paragraphStyle.alignment != TextAlignment.Center ||
			nestedText.layout.textColor != nestedColor ||
			afterText.layout.textStyle.fontSize != 22.0 ||
			afterText.layout.textColor != inheritedColor)
			return 215;
		var inheritedField = new TextField("inherited-style-field", "edit");
		var inheritedFieldRoot = context.submit(new DefaultTextStyle(inheritedField,
			TextStyleOverride.text(19.0)), new LayoutFrame(320.0, 192.0));
		if (inheritedFieldRoot.children[0].children[1].layout.textStyle.fontSize != 19.0)
			return 216;
		inheritedFieldRoot = context.submit(new DefaultTextStyle(inheritedField,
			TextStyleOverride.text(23.0)), new LayoutFrame(320.0, 192.0));
		var inheritedEditorState:State<TextEditorState> =
			context.buildContext.existingState(inheritedFieldRoot.id);
		var inheritedEditor:TextEditorState = cast inheritedEditorState.value;
		if (inheritedFieldRoot.children[0].children[1].layout.textStyle.fontSize != 23.0 ||
			inheritedEditor.textStyle.fontSize != 23.0)
			return 217;
		var themedClicks = 0;
		var themedButton = new Button("Themed", null, function() { themedClicks++; }, "theme-key");
		var themedFrame = new LayoutFrame(256.0, 192.0);
		var themedRoot = context.submit(themedButton, themedFrame);
		if (!frameMetricsValid(context, false))
			return 236;
		if (!context.focusWidget(themedRoot.id))
			return 80;
		themedRoot = context.submit(themedButton, themedFrame);
		if (themedRoot.layout.style.background.red != 0.4)
			return 81;
		themedRoot = context.submit(themedButton, themedFrame);
		if (!frameMetricsValid(context, true))
			return 237;
		if (!frameInvalidationValid(context, false, false))
			return 238;
		var themedLabelGeometry:ResolvedLayoutItem = cast themedRoot.children[0].resolved;
		var themedX = themedLabelGeometry.x + themedLabelGeometry.width * 0.5;
		var themedY = themedLabelGeometry.y + themedLabelGeometry.height * 0.5;
		context.pointerMove(themedX, themedY);
		themedRoot = context.submit(themedButton, themedFrame);
		var themedSnapshot = context.inspect();
		if (themedRoot.layout.style.background.red != 0.8 || !themedSnapshot[0].hovered ||
			themedSnapshot[1].hovered)
			return 82;
		if (!frameInvalidationValid(context, true, true))
			return 239;
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
		var inspectedBackgroundSource:Null<StyleSource> = inspected[0].computedStyle == null
			? null : inspected[0].computedStyle.source(StyleProperty.Background);
		if (inspected.length != 2 || inspected[0].label != "Themed" ||
			inspected[1].parentId != inspected[0].id || context.dumpTree().length == 0 ||
			context.auditAccessibility().length != 0 || inspected[0].styleType != "button" ||
			inspected[0].computedStyle == null ||
			inspectedBackgroundSource == null || inspectedBackgroundSource.selector != "button:disabled" ||
			(inspected[0].interactionStates & StyleState.Disabled) == 0)
			return 99;
		var inspectorEffects = new StyleSheet("InspectorEffects");
		inspectorEffects.rule(StyleSelector.widget("button"), [
			StyleValue.effects(EffectChain.of([BlurEffect.withSigma(4.0)])),
			StyleValue.backdropEffects(EffectChain.of([new BrightnessEffect(1.1)])),
			StyleValue.mask(Mask.roundedRect(6.0))
		]);
		context.setStyleSheet(inspectorEffects);
		context.submit(new Button("Inspector effects", null, function() {}, "inspector-effects"),
			new LayoutFrame(256.0, 192.0));
		var effectSnapshot = context.inspect()[0];
		if (!effectSnapshot.causesIsolation || effectSnapshot.effectPasses != 3 ||
			effectSnapshot.backdropEffectPasses != 1 || effectSnapshot.maskPasses != 1 ||
			effectSnapshot.effectDescriptions.length != 1 ||
			effectSnapshot.backdropEffectDescriptions.length != 1 ||
			effectSnapshot.isolationReasons.join(",") != "effects,backdrop-effects,mask" ||
			effectSnapshot.estimatedIntermediateTargets != 5 ||
			effectSnapshot.inkOverflow.left != 12.0 ||
			effectSnapshot.paintBounds.width <= effectSnapshot.bounds.width ||
			effectSnapshot.estimatedRenderTargetBytes <= 0.0)
			return 101;
		context.setStyleSheet(new StyleSheet("Application"));
		var unnamedSemantics:Semantics = cast themedRoot.semantics;
		unnamedSemantics.label = "";
		if (AccessibilityAudit.isValid(themedRoot))
			return 100;
		var navigationTokens = new ThemeTokens();
		navigationTokens.navigationBackground = Color.rgba(0.11, 0.12, 0.13, 1.0);
		navigationTokens.navigationHover = Color.rgba(0.31, 0.32, 0.33, 1.0);
		context.setTheme(new Theme(navigationTokens));
		var navigationButton = new Button("Navigation");
		navigationButton.variant = ButtonVariant.Navigation;
		var navigationRoot = context.submit(navigationButton, themedFrame);
		if (navigationRoot.styleClasses.indexOf("navigation") < 0 ||
			navigationRoot.layout.style.background.red != 0.11)
			return 240;
		var navigationGeometry:ResolvedLayoutItem = cast navigationRoot.resolved;
		context.pointerMove(navigationGeometry.x + 2.0, navigationGeometry.y + 2.0);
		navigationRoot = context.submit(navigationButton, themedFrame);
		if (navigationRoot.layout.style.background.red != 0.31)
			return 241;

		// Remaining widgets resolve through the same application cascade, including
		// paint-only properties consumed by their retained custom painters.
		var migratedStyles = new StyleSheet("MigratedWidgets");
		migratedStyles.rule(StyleSelector.widget("slider"), [
			StyleValue.sliderTrackColor(Color.rgba(0.11, 0.12, 0.14, 1.0)),
			StyleValue.sliderFillColor(Color.rgba(0.91, 0.31, 0.18, 1.0)),
			StyleValue.sliderThumbColor(Color.rgba(0.98, 0.98, 0.98, 1.0))
		]);
		migratedStyles.rule(StyleSelector.widget("progress-bar"), [
			StyleValue.progressTrackColor(Color.rgba(0.12, 0.13, 0.15, 1.0)),
			StyleValue.progressFillColor(Color.rgba(0.20, 0.78, 0.42, 1.0))
		]);
		migratedStyles.rule(StyleSelector.widget("popup-content"), [
			StyleValue.background(Color.rgba(0.18, 0.12, 0.22, 1.0))
		]);
		migratedStyles.rule(StyleSelector.widget("tooltip"), [
			StyleValue.background(Color.rgba(0.92, 0.72, 0.12, 1.0))
		]);
		context.setStyleSheet(migratedStyles);
		var migratedSlider = new Slider("migrated-slider", "Level", 0.5);
		var migratedSliderRoot = context.submit(migratedSlider, new LayoutFrame(256.0, 192.0));
		var sliderFillSource:Null<StyleSource> = migratedSliderRoot.computedStyle == null ? null :
			migratedSliderRoot.computedStyle.source(StyleProperty.SliderFillColor);
		if (migratedSliderRoot.computedStyle == null ||
			migratedSliderRoot.computedStyle.get(StyleProperty.SliderFillColor).red != 0.91 ||
			sliderFillSource == null || sliderFillSource.stylesheet != "MigratedWidgets" ||
			migratedSliderRoot.styleType != "slider")
			return 226;
		var migratedProgressRoot = context.submit(new ProgressBar("migrated-progress", 0.5),
			new LayoutFrame(256.0, 192.0));
		var progressFillSource:Null<StyleSource> = migratedProgressRoot.computedStyle == null ? null :
			migratedProgressRoot.computedStyle.source(StyleProperty.ProgressFillColor);
		if (migratedProgressRoot.computedStyle == null ||
			migratedProgressRoot.computedStyle.get(StyleProperty.ProgressFillColor).green != 0.78 ||
			progressFillSource == null || progressFillSource.stylesheet != "MigratedWidgets")
			return 227;
		var migratedPopupRoot = context.submit(new Popup("migrated-popup", new Text("Popup")),
			new LayoutFrame(256.0, 192.0));
		var migratedPopupPanel = migratedPopupRoot.children[1];
		var popupBackgroundSource:Null<StyleSource> = migratedPopupPanel.computedStyle == null ? null :
			migratedPopupPanel.computedStyle.source(StyleProperty.Background);
		if (migratedPopupPanel.computedStyle == null ||
			migratedPopupPanel.computedStyle.get(StyleProperty.Background).red != 0.18 ||
			popupBackgroundSource == null || popupBackgroundSource.stylesheet != "MigratedWidgets" ||
			migratedPopupPanel.styleType != "popup-content")
			return 228;
		var migratedTooltipRoot = context.submit(
			new Tooltip("migrated-tooltip", new Text("Anchor"), new Text("Hint")),
			new LayoutFrame(256.0, 192.0));
		var migratedTooltipNode = migratedTooltipRoot.children[1];
		var tooltipBackgroundSource:Null<StyleSource> = migratedTooltipNode.computedStyle == null ? null :
			migratedTooltipNode.computedStyle.source(StyleProperty.Background);
		if (migratedTooltipNode.computedStyle == null ||
			migratedTooltipNode.computedStyle.get(StyleProperty.Background).red != 0.92 ||
			tooltipBackgroundSource == null || tooltipBackgroundSource.stylesheet != "MigratedWidgets")
			return 229;
		context.setStyleSheet(new StyleSheet("Application"));

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

		// Transform animation is a composite-only change: the resolved box keeps
		// its layout size while the visual transform changes between submits.
		var transformStyle = new LayoutStyle();
		transformStyle.width = LayoutAxis.fixed(120.0);
		transformStyle.height = LayoutAxis.fixed(48.0);
		var transformCanvas = CanvasView.simple("transform-animation", function(_) {},
			transformStyle);
		var transformFrame = new LayoutFrame(256.0, 192.0);
		transformFrame.deltaSeconds = 0.0;
		var transformRoot = context.submit(transformCanvas, transformFrame);
		var initialTransformGeometry:ResolvedLayoutItem = cast transformRoot.resolved;
		var transformWidth = initialTransformGeometry.width;
		var transformHeight = initialTransformGeometry.height;
		var initialContentRevision = transformRoot.contentRevision;
		var initialGeometryRevision = transformRoot.geometryRevision;
		var initialCompositeRevision = transformRoot.compositeRevision;
		var transformAnimation = new AnimationController(context.animations, function(value) {
			transformCanvas.style.transform = Transform2D.translation(value, 0.0);
		});
		transformAnimation.play(0.0, 48.0, 1.0, Easing.EaseInOut);
		transformFrame.deltaSeconds = 0.5;
		transformRoot = context.submit(transformCanvas, transformFrame);
		var transformGeometry:ResolvedLayoutItem = cast transformRoot.resolved;
		var transformMetrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (transformMetrics == null || transformMetrics.layoutInvalidatedNodes != 0 ||
			!transformMetrics.nativeLayoutSubmitted || transformMetrics.nativeLayoutReused ||
			transformMetrics.compositeInvalidatedNodes <= 0 ||
			transformMetrics.hitGeometryInvalidatedNodes <= 0 ||
			transformGeometry.width != transformWidth ||
			transformGeometry.height != transformHeight ||
			transformGeometry.transform.tx == 0.0 ||
			transformRoot.contentRevision != initialContentRevision ||
			transformRoot.geometryRevision <= initialGeometryRevision ||
			transformRoot.compositeRevision <= initialCompositeRevision)
			return 241;
		var opacitySheet = new StyleSheet("RevisionOpacity");
		opacitySheet.rule(StyleSelector.widget("canvas"), [StyleValue.opacity(0.5)]);
		context.setStyleSheet(opacitySheet);
		transformFrame.deltaSeconds = 0.0;
		var opacityRoot = context.submit(transformCanvas, transformFrame);
		if (opacityRoot.geometryRevision != transformRoot.geometryRevision ||
			opacityRoot.contentRevision != transformRoot.contentRevision ||
			opacityRoot.compositeRevision <= transformRoot.compositeRevision ||
			!UiDirtyFlag.contains(opacityRoot.invalidationFlags, UiDirtyFlag.NeedsComposite) ||
			UiDirtyFlag.contains(opacityRoot.invalidationFlags, UiDirtyFlag.NeedsHitGeometry)) {
				Sys.println('revision opacity check failed content=${opacityRoot.contentRevision}/${transformRoot.contentRevision} geometry=${opacityRoot.geometryRevision}/${transformRoot.geometryRevision} composite=${opacityRoot.compositeRevision}/${transformRoot.compositeRevision} flags=${opacityRoot.invalidationFlags}');
				return 242;
			}
		var opacityMetrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (opacityMetrics == null || opacityMetrics.nativeLayoutSubmitted ||
			!opacityMetrics.nativeLayoutReused || opacityMetrics.resolvedGeometryChangedNodes != 0 ||
			opacityMetrics.resolvedGeometryReusedNodes != 1)
			return 243;
		context.setStyleSheet(new StyleSheet("Application"));
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

		var gradientCanvas = new Canvas();
		var gradientList = DisplayList.create();
		gradientCanvas.fillLinearGradientRect(new Rect(0.0, 0.0, 32.0, 16.0),
			0.0, 0.0, 32.0, 0.0, [
				new GradientStop(0.0, Color.rgba(0.2, 0.4, 0.8, 1.0)),
				new GradientStop(1.0, Color.rgba(0.2, 0.8, 0.4, 1.0))
			]);
		gradientCanvas.update(gradientList);
		if (gradientList.info().commandCount != 2)
			return 235;
		gradientCanvas.reset();
		gradientList.dispose();
		var gradientDecorationCanvas = new Canvas();
		var gradientDecorationList = DisplayList.create();
		new GradientDecoration(Color.rgba(0.2, 0.4, 0.8, 1.0),
			Color.rgba(0.2, 0.8, 0.4, 1.0)).paint(gradientDecorationCanvas,
			new ResolvedLayoutItem(602, 1, 0.0, 0.0, 32.0, 16.0,
				new Rect(0.0, 0.0, 32.0, 16.0), new Rect(0.0, 0.0, 32.0, 16.0),
				Transform2D.identity(), 0.0), new ComputedStyle());
		gradientDecorationCanvas.update(gradientDecorationList);
		if (gradientDecorationList.info().commandCount != 2)
			return 264;
		gradientDecorationList.dispose();

		var cachedBuilds = 0;
		var cachedFrame = new LayoutFrame(256.0, 192.0);
		var cachedRoot = context.submitCached(function() {
			cachedBuilds++;
			return new Text("Cached");
		}, cachedFrame, "framework-cache");
		var reusedRoot = context.submitCached(function() {
			cachedBuilds++;
			return new Text("Should not build");
		}, cachedFrame, "framework-cache");
		var cachedMetrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (cachedBuilds != 1 || reusedRoot != cachedRoot || cachedMetrics == null ||
			!cachedMetrics.reusedSubmission || cachedMetrics.styleResolutions != 0)
			return 109;
		cachedFrame.deltaSeconds = 0.1;
		var jitteredRoot = context.submitCached(function() {
			cachedBuilds++;
			return new Text("Should still reuse on timing jitter");
		}, cachedFrame, "framework-cache");
		cachedMetrics = context.frameMetrics;
		if (cachedBuilds != 1 || jitteredRoot != cachedRoot || cachedMetrics == null ||
			!cachedMetrics.reusedSubmission)
			return 110;
		if (!checkWindowChrome(context))
			return 231;

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

	static function decorationStyleContract():Int {
		var decorationsA = DecorationChain.of([
			new BackgroundDecoration(Color.rgba(0.1, 0.2, 0.3, 1.0)),
			new GradientDecoration(Color.rgba(0.0, 0.2, 0.8, 1.0),
				Color.rgba(0.2, 0.8, 0.4, 1.0)),
			new BorderDecoration(Color.rgba(0.8, 0.9, 1.0, 1.0), 2.0)
		]);
		var decorationsB = DecorationChain.of([
			new BackgroundDecoration(Color.rgba(0.8, 0.2, 0.1, 1.0)),
			new GradientDecoration(Color.rgba(1.0, 0.2, 0.0, 1.0),
				Color.rgba(0.8, 0.1, 0.5, 1.0)),
			new BorderDecoration(Color.rgba(1.0, 0.8, 0.2, 1.0), 4.0)
		]);
		if (decorationsA.decorations.length != 3 || !decorationsA.isEqual(decorationsA.copy()) ||
			decorationsA.isEqual(decorationsB))
			return 260;
		var directMid:GradientDecoration = cast DecorationChain.interpolate(decorationsA, decorationsB,
			0.5).decorations[1];
		if (directMid == null || Math.abs(directMid.start.red - 0.5) > 0.00001 ||
			Math.abs(directMid.end.green - 0.45) > 0.00001)
			return 264;
		var customDecorationA = new Decoration();
		var customDecorationB = new Decoration();
		var customChainA = DecorationChain.of([customDecorationA]);
		var customChainB = DecorationChain.of([customDecorationB]);
		var customEarly = DecorationChain.interpolate(customChainA, customChainB, 0.25);
		var customLate = DecorationChain.interpolate(customChainA, customChainB, 0.75);
		if (!customChainA.isEqual(customChainA.copy()) || customChainA.isEqual(customChainB) ||
			customEarly.decorations[0] != customDecorationA ||
			customLate.decorations[0] != customDecorationB)
			return 265;
		var decorationSheet = new StyleSheet("DecorationSheet");
		decorationSheet.rule(StyleSelector.widget("panel"),
			[StyleValue.decorations(decorationsA)]);
		decorationSheet.rule(StyleSelector.widget("panel").state(StyleState.Hovered),
			[StyleValue.decorations(decorationsB)]);
		var decorationResolver = new StyleResolver();
		var decorationTarget = new StyleTarget("panel", "decoration-key", "decoration-id",
			null, null, 0);
		var normalDecorated = decorationResolver.resolve(decorationTarget, null, null,
			decorationSheet);
		decorationTarget = new StyleTarget("panel", "decoration-key", "decoration-id",
			null, null, StyleState.Hovered);
		var hoveredDecorated = decorationResolver.resolve(decorationTarget, null, null,
			decorationSheet);
		var decorationSource = hoveredDecorated.source(StyleProperty.Decorations);
		if (!normalDecorated.get(StyleProperty.Decorations).isEqual(decorationsA) ||
			!hoveredDecorated.get(StyleProperty.Decorations).isEqual(decorationsB) ||
			decorationSource == null || decorationSource.selector != "panel:hovered" ||
			StyleDiff.compare(normalDecorated, hoveredDecorated).impact != StyleImpact.Paint)
			return 261;
		var decorationTransitionSheet = new StyleSheet("DecorationTransitionSheet");
		decorationTransitionSheet.rule(StyleSelector.widget("panel"),
			[StyleValue.decorations(decorationsA)]);
		decorationTransitionSheet.rule(StyleSelector.widget("panel").state(StyleState.Hovered),
			[StyleValue.decorations(decorationsB)]);
		decorationTransitionSheet.transition(StyleProperty.Decorations, 0.1, Easing.Linear);
		var decorationScheduler = new AnimationScheduler();
		var decorationTransitionResolver = new StyleResolver(decorationScheduler);
		var decorationTransitionTarget = new StyleTarget("panel", "decoration-transition-key",
			"decoration-transition-id", null, null, 0);
		decorationTransitionResolver.resolve(decorationTransitionTarget, null, null,
			decorationTransitionSheet);
		decorationTransitionTarget = new StyleTarget("panel", "decoration-transition-key",
			"decoration-transition-id", null, null, StyleState.Hovered);
		decorationTransitionResolver.resolve(decorationTransitionTarget, null, null,
			decorationTransitionSheet);
		if (decorationScheduler.activeCount != 1)
			return 262;
		decorationScheduler.advance(0.05);
		var midDecorated = decorationTransitionResolver.resolve(decorationTransitionTarget, null, null,
			decorationTransitionSheet);
		var midChain:DecorationChain = midDecorated.get(StyleProperty.Decorations);
		var midGradient:GradientDecoration = cast midChain.decorations[1];
		if (midGradient == null || Math.abs(midGradient.start.red - 0.5) > 0.00001 ||
			Math.abs(midGradient.end.green - 0.45) > 0.00001)
			return 263;
		return 0;
	}

	static function checkWindowChrome(context:UiContext):Bool {
		var runtime:Null<NativeKitRuntime> = null;
		var attached = false;
		var result = false;
		try {
			var initOptions = new InitOptions();
			initOptions.set_api_version(NativeKit.nk_api_version());
			runtime = NativeKitRuntime.start(initOptions);
			var windowOptions = new WindowOptions();
			windowOptions.set_width(320);
			windowOptions.set_height(192);
			windowOptions.set_title("NativeKit UI chrome smoke");
			windowOptions.set_flags(WindowFlags.Hidden | WindowFlags.Borderless);
			windowOptions.set_owner(NativeKit.WindowHandle.invalid());
			windowOptions.set_kind(WindowKind.Normal);
			var window = runtime.createWindow(windowOptions);
			context.attachPlatformWindow(window.nativeHandle());
			attached = true;
			var root = context.submit(new WindowChrome("chrome", WindowDecorationRegionKind.Drag,
				new Row("chrome-row", [new KeyedView("client", new WindowChrome(
					"client", WindowDecorationRegionKind.Client, new Text("Client")))])),
				new LayoutFrame(320.0, 192.0));
			result = root.windowDecoration == WindowDecorationRegionKind.Drag &&
				root.children.length == 1 &&
				root.children[0].windowDecoration == WindowDecorationRegionKind.Client &&
				root.cursor == UiCursorShape.Arrow &&
				root.children[0].cursor == UiCursorShape.Arrow &&
				root.resolved != null && root.resolved.width > 0.0 && root.resolved.height > 0.0;
			var diagonal = context.submit(new WindowChrome("chrome-diagonal",
				WindowDecorationRegionKind.ResizeNortheast, new Text("Resize")),
				new LayoutFrame(320.0, 192.0));
			result = result && diagonal.cursor == UiCursorShape.DiagonalResizeNesw &&
				diagonal.windowDecorationCursor == null;
			var custom = context.submit(new WindowChrome("chrome-custom",
				WindowDecorationRegionKind.Client, new Text("Custom"), UiCursorShape.Hand),
				new LayoutFrame(320.0, 192.0));
			result = result && custom.cursor == UiCursorShape.Hand &&
				custom.windowDecorationCursor == UiCursorShape.Hand;
		} catch (_:Dynamic) {
		}
		if (attached) {
			try {
				context.detachPlatformWindow();
			} catch (_:Dynamic) {}
		}
		if (runtime != null) {
			try {
				runtime.dispose();
			} catch (_:Dynamic) {}
		}
		return result;
	}

	static function checkSplitKeyboard(context:UiContext):Bool {
		var frame = new LayoutFrame(320.0, 192.0);
		var trailingOptions = new SplitViewOptions();
		trailingOptions.resizableSide = SplitSide.Trailing;
		trailingOptions.extent = 96.0;
		trailingOptions.minimumExtent = 64.0;
		trailingOptions.maximumExtent = 144.0;
		var trailingSplit = new SplitView("keyboard-trailing-split", new Text("Leading"),
			new Text("Trailing"), trailingOptions);
		var trailingRoot = context.submit(trailingSplit, frame);
		var trailingDivider = trailingRoot.children[1];
		var trailingSemantics:Semantics = cast trailingDivider.semantics;
		if (trailingSemantics == null || !trailingDivider.focusable ||
			(trailingSemantics.actions & AccessibilityAction.Increment) == 0 ||
			(trailingSemantics.actions & AccessibilityAction.Decrement) == 0 ||
			trailingSemantics.numericValue != 96.0)
			return false;
		if (!context.focusWidget(trailingDivider.id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Right);
		if (trailingSplit.extent != 88.0)
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Home);
		if (trailingSplit.extent != 64.0)
			return false;
		context.key(UiEventKind.KeyDown, UiKey.End);
		if (trailingSplit.extent != 144.0)
			return false;
		trailingRoot = context.submit(trailingSplit, frame);
		if (!context.accessibilityAction(trailingRoot.children[1].id.value,
			AccessibilityRequest.Decrement, null, -1, -1, 2) || trailingSplit.extent != 128.0)
			return false;

		var leadingResizes = 0;
		var leadingExpansions = 0;
		var leadingOptions = new SplitViewOptions();
		leadingOptions.resizableSide = SplitSide.Leading;
		leadingOptions.extent = 96.0;
		leadingOptions.minimumExtent = 64.0;
		leadingOptions.maximumExtent = 144.0;
		leadingOptions.onResize = function(value) { leadingResizes = Std.int(value); };
		leadingOptions.onCollapsedChanged = function(collapsed) {
			if (!collapsed)
				leadingExpansions++;
		};
		var leadingSplit = new SplitView("keyboard-leading-split", new Text("Leading"),
			new Text("Trailing"), leadingOptions);
		var leadingRoot = context.submit(leadingSplit, frame);
		if (leadingRoot.children.length != 3 ||
			leadingRoot.children[0].layout.style.width.sizing != LayoutSizing.Fixed ||
			leadingRoot.children[2].layout.style.width.sizing != LayoutSizing.Grow)
			return false;
		var leadingDivider = leadingRoot.children[1];
		var leadingSemantics:Semantics = cast leadingDivider.semantics;
		if (leadingSemantics == null || leadingSemantics.label != "Leading pane divider" ||
			leadingSemantics.numericValue != 96.0)
			return false;
		var leadingGeometry:ResolvedLayoutItem = cast leadingDivider.resolved;
		var leadingX = leadingGeometry.x + leadingGeometry.width * 0.5;
		var leadingY = leadingGeometry.y + leadingGeometry.height * 0.5;
		context.pointerDown(leadingX, leadingY, 0);
		context.pointerMove(leadingX + 20.0, leadingY);
		context.pointerUp(leadingX + 20.0, leadingY, 0);
		if (leadingResizes != 116 || leadingSplit.extent != 116.0)
			return false;
		leadingSplit.collapsed = true;
		leadingRoot = context.submit(leadingSplit, frame);
		var collapsedLeadingGeometry:ResolvedLayoutItem = cast leadingRoot.children[0].resolved;
		if (leadingRoot.children[0].layout.style.visible ||
			collapsedLeadingGeometry.width != 0.0)
			return false;
		var collapsedDividerGeometry:ResolvedLayoutItem = cast leadingRoot.children[1].resolved;
		var collapsedX = collapsedDividerGeometry.x + collapsedDividerGeometry.width * 0.5;
		var collapsedY = collapsedDividerGeometry.y + collapsedDividerGeometry.height * 0.5;
		context.pointerDown(collapsedX, collapsedY, 0);
		context.pointerMove(collapsedX + 20.0, collapsedY);
		context.pointerUp(collapsedX + 20.0, collapsedY, 0);
		if (leadingExpansions != 1 || leadingSplit.collapsed || leadingSplit.extent != 84.0)
			return false;
		leadingRoot = context.submit(leadingSplit, frame);
		if (!context.focusWidget(leadingRoot.children[1].id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Right);
		if (leadingSplit.extent != 92.0)
			return false;
		leadingRoot = context.submit(leadingSplit, frame);
		if (!context.accessibilityAction(leadingRoot.children[1].id.value,
			AccessibilityRequest.Decrement, null, -1, -1, 2) || leadingSplit.extent != 76.0)
			return false;
		return true;
	}

	static function frameMetricsValid(context:UiContext, requireCacheHit:Bool):Bool {
		var metrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (metrics == null || metrics.frameNumber <= 0 || metrics.nodeCount != 2 ||
			metrics.styleResolutions <= 0 || metrics.submitSeconds < 0.0 || metrics.totalSeconds < 0.0)
			return false;
		return !requireCacheHit || (metrics.styleCacheHits > 0 && metrics.styleCacheMisses == 0);
	}

	static function frameInvalidationValid(context:UiContext, requireChanged:Bool,
			requirePaint:Bool):Bool {
		var metrics:Null<UiFrameMetrics> = context.frameMetrics;
		if (metrics == null || metrics.styleChangedNodes < 0 || metrics.styleUnchangedNodes < 0 ||
			metrics.layoutInvalidatedNodes < 0 || metrics.textLayoutInvalidatedNodes < 0 ||
			metrics.paintInvalidatedNodes < 0 || metrics.compositeInvalidatedNodes < 0 ||
			metrics.semanticsInvalidatedNodes < 0)
			return false;
		if (!requireChanged && metrics.styleChangedNodes != 0)
			return false;
		if (!requireChanged && metrics.styleInvalidationFlags != UiDirtyFlag.None)
			return false;
		if (requireChanged && metrics.styleChangedNodes <= 0)
			return false;
		if (requireChanged && !UiDirtyFlag.contains(metrics.styleInvalidationFlags, UiDirtyFlag.NeedsStyle))
			return false;
		if (requirePaint && !UiDirtyFlag.contains(metrics.styleInvalidationFlags, UiDirtyFlag.NeedsPaint))
			return false;
		return !requirePaint || metrics.paintInvalidatedNodes > 0;
	}

	static function coordinateMathValid():Bool {
		var local = new Point(23.0, 11.0);
		var transforms:Array<Transform2D> = [
			Transform2D.translation(40.0, 25.0),
			Transform2D.identity().scaled(1.5, 0.75).translated(12.0, -8.0),
			Transform2D.identity().rotated(0.37).translated(18.0, 9.0),
			Transform2D.identity().skewed(0.17, -0.11).translated(-6.0, 14.0)
		];
		for (transform in transforms) {
			var item = new ResolvedLayoutItem(900, 1, 17.0, 13.0, 120.0, 64.0,
				new Rect(-1000.0, -1000.0, 2000.0, 2000.0),
				new Rect(0.0, 0.0, 0.0, 0.0), transform, 0.0);
			var global = item.localToViewport(local);
			var roundTrip = item.viewportToLocal(global);
			if (!near(roundTrip.x, local.x) || !near(roundTrip.y, local.y) ||
				!item.hitTest(global.x, global.y))
				return false;
		}

		var parent = Transform2D.translation(100.0, 40.0).rotated(0.23).scaled(1.2, 0.8);
		var child = Transform2D.translation(12.0, 7.0).skewed(0.09, -0.04);
		var world = parent.multiply(child);
		var worldPoint = world.transformPoint(local);
		var nestedRoundTrip = world.inverse().transformPoint(worldPoint);
		if (!near(nestedRoundTrip.x, local.x) || !near(nestedRoundTrip.y, local.y))
			return false;

		var singular = Transform2D.scale(0.0, 1.0);
		if (singular.isInvertible() || singular.tryInverse() != null)
			return false;
		var singularItem = new ResolvedLayoutItem(901, 1, 0.0, 0.0, 20.0, 20.0,
				new Rect(-100.0, -100.0, 200.0, 200.0),
				new Rect(0.0, 0.0, 0.0, 0.0), singular, 0.0);
		return !singularItem.hitTest(0.0, 0.0) && nestedSceneSemanticsValid();
	}

	/** Covers the shared Haxe scene policy around transforms, clipping, order, and events. */
	static function nestedSceneSemanticsValid():Bool {
		var broadClip = new Rect(-1000.0, -1000.0, 3000.0, 3000.0);
		var root = new RenderNode(new WidgetId(910));
		var panel = new RenderNode(new WidgetId(911));
		var target = new RenderNode(new WidgetId(912));
		root.hitTestSelf = false;
		panel.hitTestSelf = false;
		root.add(panel);
		panel.add(target);
		var rootTransform = Transform2D.translation(100.0, 40.0);
		var panelTransform = rootTransform.multiply(Transform2D.rotation(0.21));
		var targetTransform = panelTransform.multiply(Transform2D.identity().skewed(0.08, -0.05));
		root.resolved = new ResolvedLayoutItem(910, 1, 0.0, 0.0, 400.0, 300.0,
			broadClip, new Rect(0.0, 0.0, 0.0, 0.0), rootTransform, 0.0);
		panel.resolved = new ResolvedLayoutItem(911, 1, 20.0, 30.0, 180.0, 120.0,
			broadClip, new Rect(0.0, 0.0, 0.0, 0.0), panelTransform, 0.0);
		target.resolved = new ResolvedLayoutItem(912, 1, 10.0, 15.0, 60.0, 40.0,
			broadClip, new Rect(0.0, 0.0, 0.0, 0.0), targetTransform, 0.0);
		var localPoint = new Point(8.0, 9.0);
		var globalPoint = target.localToGlobal(localPoint);
		var expectedRoot = root.globalToLocal(globalPoint);
		var expectedPanel = panel.globalToLocal(globalPoint);
		var order:Array<String> = [];
		var rootLocalX = 0.0;
		var panelLocalX = 0.0;
		var targetLocalX = 0.0;
		root.on(UiEventKind.PointerDown, function(event) {
			order.push("root-capture");
			rootLocalX = event.localX;
		}, "capture");
		panel.on(UiEventKind.PointerDown, function(event) {
			order.push("panel-capture");
			panelLocalX = event.localX;
		}, "capture");
		target.on(UiEventKind.PointerDown, function(event) {
			order.push("target");
			targetLocalX = event.localX;
		});
		panel.on(UiEventKind.PointerDown, function(event) {
			order.push("panel-bubble");
			if (!near(event.localX, expectedPanel.x))
				order.push("panel-coordinate-error");
		});
		root.on(UiEventKind.PointerDown, function(event) {
			order.push("root-bubble");
			if (!near(event.localX, expectedRoot.x))
				order.push("root-coordinate-error");
		});
		var dispatcher = new EventDispatcher(new FocusManager(), new InteractionStateStore());
		dispatcher.setRoot(root);
		dispatcher.pointerDown(globalPoint.x, globalPoint.y, 0);
		if (order.join(",") != "root-capture,panel-capture,target,panel-bubble,root-bubble" ||
			!near(rootLocalX, expectedRoot.x) || !near(panelLocalX, expectedPanel.x) ||
			!near(targetLocalX, localPoint.x) || HitTest.path(root, globalPoint.x, globalPoint.y).length != 3)
			return false;

		// An inverse-transformed child remains clipped in viewport space, even
		// when its own local bounds are reached by the pointer.
		var clippedGlobal = target.localToGlobal(new Point(8.0, 32.0));
		target.resolved = new ResolvedLayoutItem(912, 1, 10.0, 15.0, 60.0, 40.0,
			new Rect(globalPoint.x - 1.0, globalPoint.y - 1.0, 2.0, 2.0),
			new Rect(0.0, 0.0, 0.0, 0.0), targetTransform, 0.0);
		if (HitTest.path(root, clippedGlobal.x, clippedGlobal.y).length != 0)
			return false;

		var stack = new RenderNode(new WidgetId(920));
		stack.hitTestSelf = false;
		stack.resolved = new ResolvedLayoutItem(920, 1, 0.0, 0.0, 100.0, 100.0,
			broadClip, new Rect(0.0, 0.0, 0.0, 0.0), Transform2D.identity(), 0.0);
		var lower = new RenderNode(new WidgetId(921));
		var upper = new RenderNode(new WidgetId(922));
		var equalFirst = new RenderNode(new WidgetId(923));
		var equalSecond = new RenderNode(new WidgetId(924));
		var flowFirst = new RenderNode(new WidgetId(925));
		var flowSecond = new RenderNode(new WidgetId(926));
		for (child in [lower, upper, equalFirst, equalSecond, flowFirst, flowSecond]) {
			child.resolved = new ResolvedLayoutItem(child.id.value, 1, 0.0, 0.0, 100.0, 100.0,
				broadClip, new Rect(0.0, 0.0, 0.0, 0.0), Transform2D.identity(), 0.0);
			child.layout.style.positioning = LayoutPositioning.Absolute;
		}
		lower.layout.style.zIndex = 1;
		upper.layout.style.zIndex = 5;
		equalFirst.layout.style.zIndex = 7;
		equalSecond.layout.style.zIndex = 7;
		// Flow zIndex is intentionally ignored by the native renderer.
		flowFirst.layout.style.positioning = LayoutPositioning.Flow;
		flowFirst.layout.style.zIndex = 100;
		flowSecond.layout.style.positioning = LayoutPositioning.Flow;
		flowSecond.layout.style.zIndex = -100;
		stack.add(lower);
		stack.add(upper);
		stack.add(equalFirst);
		stack.add(equalSecond);
		var hitPath = HitTest.path(stack, 10.0, 10.0);
		if (hitPath.length != 2 || !hitPath[1].id.equals(equalSecond.id))
			return false;
		var flowStack = new RenderNode(new WidgetId(927));
		flowStack.hitTestSelf = false;
		flowStack.resolved = stack.resolved;
		flowStack.add(flowFirst);
		flowStack.add(flowSecond);
		hitPath = HitTest.path(flowStack, 10.0, 10.0);
		return hitPath.length == 2 && hitPath[1].id.equals(flowSecond.id);
	}

	static inline function near(left:Float, right:Float):Bool
		return Math.abs(left - right) < 0.00001;
}

private class SmokeListModel implements ListViewModel {
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
		return 'model-item:$index';

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
		return new Text('Model row $index');
	}

	public function revision():Int
		return modelRevision;

	public function offsetBefore(index:Int):Float {
		var result = 0.0;
		for (item in 0...index)
			result += 20.0 + (item % 3) * 4.0;
		return result;
	}
}

private class SmokeTreeModel implements TreeViewModel {
	final builtKeys:Array<String>;

	public function new(builtKeys:Array<String>)
		this.builtKeys = builtKeys;

	public function rootCount():Int
		return 100000;

	public function rootKeyAt(index:Int):String
		return 'root:$index';

	public function childCount(parentKey:String):Int
		return parentKey == "root:0" ? 3 : 0;

	public function childKeyAt(parentKey:String, index:Int):String
		return '$parentKey:child:$index';

	public function initiallyExpanded(key:String):Bool
		return key == "root:0";

	public function extentAt(key:String):Float
		return key.indexOf(":child:") >= 0 ? 28.0 : 24.0;

	public function buildItem(key:String):View {
		builtKeys.push(key);
		return new Text(key);
	}

	public function revision():Int
		return 1;
}
