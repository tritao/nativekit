package nativekit.ui.widgets;

import Canvas;
import Color;
import Insets;
import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import ParagraphStyle;
import Rect;
import ResolvedLayoutItem;
import TextStyle;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.ResolvedTextStyle;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleSource;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.TextRole;

/** Text editor composed from a Haxe box and NativeUI text primitive. */
class TextField implements View {
	public final key:String;
	public var value:String;
	public var label:Null<String>;
	/** Muted visual hint shown only while the editor is empty. */
	public var placeholder:Null<String>;
	public final multiline:Bool;
	public final style:LayoutStyle;
	public final textStyle:Null<TextStyle>;
	public final textColor:Null<Color>;
	/** Typed selector classes used by composite fields such as ComboBox. */
	public var classes:Array<String>;
	public var enabled:Bool;
	public var onChange:Null<String->Void>;
	public var onSubmit:Null<String->Void>;
	public var onDiagnostics:Null<TextEditorDiagnostics->Void>;
	/** Semantic role override used by composite editable controls. */
	public var semanticRole:AccessibilityRole;
	/** Semantic action capabilities override used by composite editable controls. */
	public var semanticActions:Int;

	public function new(key:String, value:String = "", ?onChange:String->Void,
			?style:LayoutStyle, ?label:String, ?textStyle:TextStyle, ?textColor:Color,
			multiline:Bool = false) {
		if (key == null || key.length == 0)
			throw "Text fields require a stable key";
		this.key = key;
		this.value = value == null ? "" : value;
		this.onChange = onChange;
		this.onSubmit = null;
		this.label = label;
		this.placeholder = null;
		this.multiline = multiline;
		this.style = style == null ? defaultStyle(multiline) : style.copy();
		this.textStyle = textStyle == null ? null :
			new TextStyle(textStyle.fontSize, textStyle.font, textStyle.letterSpacing);
		this.textColor = textColor;
		classes = [];
		enabled = true;
		onDiagnostics = null;
		semanticRole = AccessibilityRole.TextField;
		semanticActions = AccessibilityAction.SetValue | AccessibilityAction.SetSelection;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var id = context.id("field");
			var resolved = context.resolveTextRole(TextRole.Body,
				TextStyleOverride.fromTextStyle(textStyle));
			if (textColor != null)
				resolved = resolved.withTextColor(textColor);
			var paragraph = new ParagraphStyle(resolved.paragraphStyle.wrap,
				resolved.paragraphStyle.alignment, resolved.paragraphStyle.lineHeight,
				resolved.paragraphStyle.direction);
			paragraph.wrap = multiline ? TextWrap.WordCharacter : TextWrap.None;
			resolved = new ResolvedTextStyle(resolved.textStyle, paragraph, resolved.textColor);
			var stored:State<TextEditorState> = acquireState(context, id, value, resolved);
			var editor:TextEditorState = stored.value;
			editor.updateStyle(resolved.textStyle, resolved.paragraphStyle);
			if (editor.syncExternal(value))
				editor.resetCaretBlink(context.gestures.timeSeconds());

			var flags = context.interactionStates.get(id);
			flags = StyleStateUtil.withState(flags, StyleState.Disabled, !enabled);
			var computed = context.styleResolver.resolve(
				new StyleTarget("text-field", key, key, classes, ["text-field"], flags),
				context.inheritedStyle, context.theme.styles, context.styleSheet, style, context.environment);
			if (textColor != null)
				computed.set(StyleProperty.TextColor, textColor,
					new StyleSource("local", "text-field", -1, "local"));
			var node = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("text-field", key, key, classes, ["text-field"]);
			node.states = flags;
			node.computedStyle = computed;
			node.focusable = enabled;
			node.enabled = enabled;
			var semantics = new Semantics(semanticRole,
				label == null ? key : label, editor.text);
			semantics.actions = semanticActions;
			semantics.textStart = 0;
			semantics.documentLength = Utf8Text.length(editor.text);
			semantics.selectionStart = editor.selectionStart;
			semantics.selectionEnd = editor.selectionEnd;
			if (!enabled)
				semantics.states |= AccessibilityState.Disabled;
			if (editor.focused)
				semantics.states |= AccessibilityState.Focused;
			if (multiline)
				semantics.states |= AccessibilityState.Multiline;
			node.semantics = semantics;

			var textNodeStyle = new LayoutStyle();
			textNodeStyle.width = LayoutAxis.grow();
			textNodeStyle.height = multiline ? LayoutAxis.fit() : LayoutAxis.grow();
			var builtScrollOffset = multiline ? editor.scrollOffsetY : 0.0;
			textNodeStyle.transform = Transform2D.identity().translated(0.0,
				-builtScrollOffset);
			textNodeStyle.zIndex = 1;
			var editorContentStyle = new LayoutStyle();
			editorContentStyle.width = LayoutAxis.grow();
			editorContentStyle.height = LayoutAxis.grow();
			editorContentStyle.clipHorizontal = true;
			editorContentStyle.clipVertical = multiline;
			var editorContent = new RenderNode(context.id("editor-content"),
				LayoutVisualKind.Box, editorContentStyle);
			var selectionStyle = new LayoutStyle();
			selectionStyle.width = LayoutAxis.grow();
			selectionStyle.height = LayoutAxis.grow();
			selectionStyle.positioning = LayoutPositioning.Absolute;
			selectionStyle.zIndex = 0;
			var selectionNode = new RenderNode(context.id("editor-selection"),
				LayoutVisualKind.Custom, selectionStyle);
			selectionNode.hitTestSelf = false;
			selectionNode.onPaint(function(canvas, _) {
				if (!editor.isDisposed())
					paintSelection(canvas, editor, context.textInput.isOwner(id), context.theme);
			});
			editorContent.add(selectionNode);
			var textNode = new RenderNode(context.id("text"), LayoutVisualKind.Text, textNodeStyle);
			var showsPlaceholder = editor.layoutText().length == 0 && placeholder != null &&
				placeholder.length > 0;
			textNode.layout.text = showsPlaceholder ? placeholder : editor.layoutText();
		var textNodeTextStyle = new TextStyle(editor.textStyle.fontSize,
			editor.textStyle.font, editor.textStyle.letterSpacing);
		var fontSource = computed.source(StyleProperty.FontSize);
		var letterSource = computed.source(StyleProperty.LetterSpacing);
		if (textStyle == null && fontSource != null && fontSource.layer != "framework")
			textNodeTextStyle.fontSize = computed.get(StyleProperty.FontSize);
		if (textStyle == null && letterSource != null && letterSource.layer != "framework")
			textNodeTextStyle.letterSpacing = computed.get(StyleProperty.LetterSpacing);
		var colorSource = computed.source(StyleProperty.TextColor);
		var textNodeColor = showsPlaceholder ? context.theme.mutedText :
			(colorSource != null && colorSource.layer != "framework"
				? computed.get(StyleProperty.TextColor) : resolved.textColor);
		textNode.applyTextStyle(new ResolvedTextStyle(textNodeTextStyle,
			editor.paragraphStyle, textNodeColor));
			editorContent.add(textNode);
			var paintStyle = new LayoutStyle();
			paintStyle.width = LayoutAxis.grow();
			paintStyle.height = LayoutAxis.grow();
			paintStyle.positioning = LayoutPositioning.Absolute;
			paintStyle.zIndex = 2;
			var paintNode = new RenderNode(context.id("editor-paint"),
				LayoutVisualKind.Custom, paintStyle);
			paintNode.hitTestSelf = false;
			paintNode.onPaint(function(canvas, _) {
				if (!editor.isDisposed())
					paintEditorDecorations(canvas, editor, context.textInput.isOwner(id),
						context.theme, context.gestures.timeSeconds());
			});
			editorContent.add(paintNode);
			node.add(editorContent);

			var updateState = function() {
				value = editor.layoutText();
				semantics.value = value;
				semantics.documentLength = Utf8Text.length(value);
				semantics.selectionStart = editor.selectionStart;
				semantics.selectionEnd = editor.selectionEnd;
				stored.update(editor);
				if (multiline && editorContent.resolved != null &&
					editor.ensureCaretVisible(editorContent.resolved.height))
					stored.update(editor);
			};
			var publishTextChange = function(previous:String) {
				updateState();
				if (previous != value && onChange != null)
					onChange(value);
			};
			var publishDiagnostics = function(caretRect:Null<Rect>) {
				if (onDiagnostics == null)
					return;
				var compositionText = editor.compositionStart >= 0 &&
					editor.compositionEnd >= editor.compositionStart
					? Utf8Text.slice(editor.layoutText(), editor.compositionStart, editor.compositionEnd) : "";
				onDiagnostics(new TextEditorDiagnostics(key, label == null ? key : label,
					editor.focused, editor.selectionStart, editor.selectionEnd,
					editor.selectionFocus, editor.compositionStart, editor.compositionEnd,
					compositionText, caretRect, context.textInput.platformSupported,
					context.textInput.platformActive));
			};
			var syncCursor = function(geometry:ResolvedLayoutItem) {
				var caret = editor.layout.caret(editor.focusPosition());
				var topX = geometry.x + caret.x + caret.ascender * caret.slope;
				var topY = geometry.y + caret.y + caret.ascender;
				var bottomX = geometry.x + caret.x + caret.descender * caret.slope;
				var bottomY = geometry.y + caret.y + caret.descender;
				var transform:Transform2D = cast geometry.transform;
				var screenTopX = transform.a * topX + transform.c * topY + transform.tx;
				var screenTopY = transform.b * topX + transform.d * topY + transform.ty;
				var screenBottomX = transform.a * bottomX + transform.c * bottomY + transform.tx;
				var screenBottomY = transform.b * bottomX + transform.d * bottomY + transform.ty;
				if (multiline) {
					// Resolution callbacks can reveal the caret after the node's
					// transform was built. Keep the same-frame IME anchor in sync.
					var scrollDelta = editor.scrollOffsetY - builtScrollOffset;
					screenTopX -= transform.c * scrollDelta;
					screenTopY -= transform.d * scrollDelta;
					screenBottomX -= transform.c * scrollDelta;
					screenBottomY -= transform.d * scrollDelta;
				}
				var caretRect = new Rect(Math.min(screenTopX, screenBottomX), Math.min(screenTopY, screenBottomY),
					Math.max(1.0, absolute(screenBottomX - screenTopX)),
					Math.max(1.0, absolute(screenBottomY - screenTopY)));
				publishDiagnostics(caretRect);
				if (!editor.focused || !context.textInput.isOwner(id) || context.platformSurface == null ||
					context.platformSurface.isDisposed())
					return;
				context.textInput.update(editor.layoutText(), Utf8Text.length(editor.text),
					editor.selectionStart, editor.selectionEnd, editor.compositionStart,
					editor.compositionEnd, 0,
					multiline ? 1 : 0,
					caretRect);
			};
			editorContent.onResolved(function(geometry) {
				if (multiline) {
					editor.updateLayout(geometry.width);
					if (editor.ensureCaretVisible(geometry.height))
						stored.update(editor);
				}
			});
			textNode.onResolved(function(geometry) {
				editor.updateLayout(geometry.width);
				if (multiline && editorContent.resolved != null &&
					editor.ensureCaretVisible(editorContent.resolved.height))
					stored.update(editor);
				syncCursor(geometry);
			});
			node.on(UiEventKind.Focus, function(_) {
				if (!enabled)
					return;
				editor.focused = true;
				editor.resetCaretBlink(context.gestures.timeSeconds());
				semantics.states |= AccessibilityState.Focused;
				stored.update(editor);
				context.textInput.activate(id);
				if (textNode.resolved != null)
					syncCursor(cast textNode.resolved);
				else
					publishDiagnostics(null);
			});
			var blur = function(event:UiEvent) {
				if (!editor.focused && !editor.draggingSelection)
					return;
				editor.focused = false;
				editor.draggingSelection = false;
				editor.cancelPointerClick();
				semantics.states &= ~AccessibilityState.Focused;
				stored.update(editor);
				context.textInput.deactivate(id);
				publishDiagnostics(null);
			};
			node.on(UiEventKind.Blur, blur);
			node.on(UiEventKind.FocusLost, blur);

			node.on(UiEventKind.PointerDown, function(event) {
				if (!enabled || event.button != 0 || textNode.resolved == null)
					return;
				event.capturePointer();
				var geometry:ResolvedLayoutItem = cast textNode.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				var position = editor.hitTest(point.x - geometry.x, point.y - geometry.y);
				editor.resetCaretBlink(context.gestures.timeSeconds());
				var extend = (event.modifiers & UiModifier.Shift) != 0;
				if (extend)
					editor.cancelPointerClick();
				var clickCount = extend ? 1 : editor.registerPointerClick(position,
					context.gestures.timeSeconds(), event.x, event.y);
				var changed = switch (clickCount) {
					case 2: editor.selectWordAt(position);
					case 3: editor.selectLineAt(position);
					case _: editor.placeCaretAt(position, extend);
				};
				if (changed)
					updateState();
				editor.draggingSelection = true;
				event.preventDefault();
			});
			node.on(UiEventKind.PointerMove, function(event) {
				if (!enabled || !editor.draggingSelection || textNode.resolved == null)
					return;
				editor.cancelPointerClickIfMoved(event.x, event.y);
				var geometry:ResolvedLayoutItem = cast textNode.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				var position = editor.hitTest(point.x - geometry.x, point.y - geometry.y);
				if (editor.placeCaretAt(position, true)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					editor.cancelPointerClick();
					updateState();
				}
			});
			node.on(UiEventKind.PointerUp, function(event) {
				editor.completePointerClick(event.x, event.y);
				editor.draggingSelection = false;
			});
			node.on(UiEventKind.PointerCancel, function(_) {
				editor.cancelPointerClick();
				editor.draggingSelection = false;
			});

			var handleKey = function(event:UiEvent) {
				if (!enabled)
					return;
				var extend = (event.modifiers & UiModifier.Shift) != 0;
				var command = (event.modifiers & (UiModifier.Control | UiModifier.Super)) != 0;
				var macWordNavigation = #if (mac || ios)
					(event.modifiers & UiModifier.Alt) != 0 &&
					(event.modifiers & UiModifier.Control) == 0;
				#else
					false;
				#end
				var wordNavigation = #if (mac || ios)
					macWordNavigation;
				#else
					(event.modifiers & UiModifier.Control) != 0;
				#end
				var handled = true;
				var changed = false;
				var previousText = editor.layoutText();
				if (command && event.key == UiKey.A)
					changed = editor.selectAll();
				else if (command && event.key == UiKey.C)
					copySelection(context.clipboard, editor);
				else if (command && event.key == UiKey.X) {
					copySelection(context.clipboard, editor);
					changed = editor.replaceRange(editor.selectionStart, editor.selectionEnd, "");
				} else if (command && event.key == UiKey.V) {
					context.clipboard.readText(function(pasted) {
						if (editor.isDisposed() || !editor.focused)
							return;
						var beforePaste = editor.layoutText();
						if (editor.insert(pasted)) {
							editor.resetCaretBlink(context.gestures.timeSeconds());
							publishTextChange(beforePaste);
						}
					});
				} else if (wordNavigation && event.key == UiKey.Left)
					changed = editor.moveCaretByWord(-1, extend, macWordNavigation);
				else if (wordNavigation && event.key == UiKey.Right)
					changed = editor.moveCaretByWord(1, extend, macWordNavigation);
				else if (wordNavigation && event.key == UiKey.Up)
					changed = editor.moveCaretByParagraph(-1, extend, macWordNavigation);
				else if (wordNavigation && event.key == UiKey.Down)
					changed = editor.moveCaretByParagraph(1, extend, macWordNavigation);
				else if (multiline && event.key == UiKey.Up)
					changed = editor.moveCaretVertically(-1, extend);
				else if (multiline && event.key == UiKey.Down)
					changed = editor.moveCaretVertically(1, extend);
				#if (mac || ios)
				else if ((event.modifiers & UiModifier.Super) != 0 && event.key == UiKey.Up)
					changed = editor.placeCaret(0, extend);
				else if ((event.modifiers & UiModifier.Super) != 0 && event.key == UiKey.Down)
					changed = editor.placeCaret(Utf8Text.length(editor.text), extend);
				#end
				else if (event.key == UiKey.Left)
					changed = editor.moveCaret(-1, extend);
				else if (event.key == UiKey.Right)
					changed = editor.moveCaret(1, extend);
				else if (event.key == UiKey.Home)
					changed = multiline && !command ? editor.moveCaretToLineBoundary(false, extend) :
						editor.placeCaret(0, extend);
				else if (event.key == UiKey.End)
					changed = multiline && !command ? editor.moveCaretToLineBoundary(true, extend) :
						editor.placeCaret(Utf8Text.length(editor.text), extend);
				else if (event.key == UiKey.Backspace)
					changed = editor.deleteBackward();
				else if (event.key == UiKey.Delete)
					changed = editor.deleteForward();
				else if (event.key == UiKey.Enter) {
					if (multiline)
						changed = editor.insert("\n");
					else if (onSubmit != null)
						onSubmit(editor.layoutText());
				} else
					handled = false;
				if (changed)
					publishTextChange(previousText);
				if (handled) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					event.preventDefault();
				}
			};
			node.on(UiEventKind.KeyDown, handleKey);
			node.on(UiEventKind.KeyRepeat, handleKey);

			node.on(UiEventKind.TextInput, function(event) {
				var previousText = editor.layoutText();
				if (enabled && editor.insert(event.text)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.TextEdit, function(event) {
				if (!enabled || event.data == null)
					return;
				var edit:NativeKitTextEdit = cast event.data;
				var previousText = editor.layoutText();
				if (editor.applyTextEdit(edit)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.AccessibilitySetValue, function(event) {
				var previousText = editor.layoutText();
				if (enabled && editor.replaceRange(0, Utf8Text.length(editor.text), event.text)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.AccessibilitySetSelection, function(event) {
				if (!enabled || event.data == null)
					return;
				var request:AccessibilityActionData = cast event.data;
				if (editor.setSelection(request.selectionStart, request.selectionEnd)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					updateState();
				}
			});
			return node;
		});
	}

	static function acquireState(context:BuildContext, id:nativekit.ui.core.WidgetId,
			value:String, resolved:ResolvedTextStyle):State<TextEditorState> {
		if (context.fonts == null || context.fonts.isDisposed())
			throw "Text fields require fonts on their build context";
		var fonts = context.fonts;
		return context.resourceState(id,
			function() {
				return new TextEditorState(cast fonts, value, resolved.textStyle,
					resolved.paragraphStyle);
			},
			function(editor:TextEditorState) { editor.dispose(); });
	}

	static function copySelection(clipboard:nativekit.ui.core.ClipboardService,
			editor:TextEditorState):Void {
		if (editor.selectionStart != editor.selectionEnd)
			clipboard.writeText(Utf8Text.slice(editor.layoutText(), editor.selectionStart,
				editor.selectionEnd));
	}

	static function paintSelection(canvas:Canvas, editor:TextEditorState,
			active:Bool, theme:nativekit.ui.theme.Theme):Void {
		if (editor.selectionStart != editor.selectionEnd) {
			canvas.translate(0.0, -editor.scrollOffsetY);
			for (rect in editor.layout.selectionRects(editor.anchorPosition(), editor.focusPosition()))
				canvas.fillRectIfPositive(rect, active ? theme.textSelection : theme.textSelectionInactive);
		}
	}

	static function paintEditorDecorations(canvas:Canvas, editor:TextEditorState,
			active:Bool, theme:nativekit.ui.theme.Theme, timeSeconds:Float):Void {
		if (editor.scrollOffsetY != 0.0)
			canvas.translate(0.0, -editor.scrollOffsetY);
		if (active && editor.compositionStart >= 0 && editor.compositionStart != editor.compositionEnd) {
			for (rect in editor.compositionRects())
				canvas.fillRectIfPositive(new Rect(rect.x, rect.y + rect.height - 1.0, rect.width, 1.0),
					Color.rgba(0.95, 0.75, 0.24, 1.0));
		}
		if (active && editor.selectionStart == editor.selectionEnd &&
				editor.isCaretVisible(timeSeconds)) {
			var caret = editor.layout.caret(editor.focusPosition());
			var topX = caret.x + caret.ascender * caret.slope;
			var topY = caret.y + caret.ascender;
			var bottomX = caret.x + caret.descender * caret.slope;
			var bottomY = caret.y + caret.descender;
			var x = Math.min(topX, bottomX);
			var y = Math.min(topY, bottomY);
			var height = Math.max(1.0, absolute(bottomY - topY));
			canvas.fillRectIfPositive(new Rect(x, y, Math.max(1.0, absolute(bottomX - topX)), height),
				theme.textCaret);
		}
	}

	static inline function absolute(value:Float):Float
		return value < 0.0 ? -value : value;

	static function defaultStyle(multiline:Bool):LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(240.0);
		result.height = LayoutAxis.fixed(multiline ? 120.0 : 40.0);
		result.padding = new Insets(10.0, 8.0, 10.0, 8.0);
		result.background = Color.rgba(0.11, 0.13, 0.17, 1.0);
		result.radiusTopLeft = result.radiusTopRight = 5.0;
		result.radiusBottomLeft = result.radiusBottomRight = 5.0;
		result.clipHorizontal = true;
		return result;
	}
}
