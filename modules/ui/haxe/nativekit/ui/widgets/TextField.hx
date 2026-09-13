package nativekit.ui.widgets;

import Canvas;
import Color;
import Insets;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import ParagraphStyle;
import Rect;
import ResolvedLayoutItem;
import TextPosition;
import TextStyle;
import Transform2D;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Single-line text editor composed from a Haxe box and NativeUI text primitive. */
class TextField implements View {
	public final key:String;
	public var value:String;
	public var label:Null<String>;
	public final multiline:Bool;
	public final style:LayoutStyle;
	public final textStyle:TextStyle;
	public final textColor:Color;
	public var enabled:Bool;
	public var onChange:Null<String->Void>;
	public var onSubmit:Null<String->Void>;

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
		this.multiline = multiline;
		this.style = style == null ? defaultStyle(multiline) : style.copy();
		this.textStyle = textStyle == null ? new TextStyle() :
			new TextStyle(textStyle.fontSize, textStyle.font, textStyle.letterSpacing);
		this.textColor = textColor == null ? Color.rgba(0.96, 0.97, 0.99, 1.0) : textColor;
		enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var id = context.id("field");
			var stored:State<TextEditorState> = acquireState(context, id, value, textStyle, multiline);
			var editor:TextEditorState = cast stored.value;
			editor.syncExternal(value);

			var node = new RenderNode(id, LayoutVisualKind.Box, style);
			node.focusable = true;
			node.enabled = enabled;
			var semantics = new Semantics(AccessibilityRole.TextField,
				label == null ? key : label, editor.text);
			semantics.actions = AccessibilityAction.SetValue | AccessibilityAction.SetSelection;
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
			textNodeStyle.height = LayoutAxis.grow();
			var textNode = new RenderNode(context.id("text"), LayoutVisualKind.Text, textNodeStyle);
			textNode.layout.text = editor.layoutText();
			textNode.layout.textColor = textColor;
			textNode.layout.textStyle.font = textStyle.font;
			textNode.layout.textStyle.fontSize = textStyle.fontSize;
			textNode.layout.textStyle.letterSpacing = textStyle.letterSpacing;
			textNode.layout.paragraphStyle.wrap = editor.paragraphStyle.wrap;
			textNode.layout.paragraphStyle.alignment = editor.paragraphStyle.alignment;
			textNode.layout.paragraphStyle.direction = editor.paragraphStyle.direction;
			node.add(textNode);

			var updateState = function() {
				value = editor.layoutText();
				semantics.value = value;
				semantics.documentLength = Utf8Text.length(value);
				semantics.selectionStart = editor.selectionStart;
				semantics.selectionEnd = editor.selectionEnd;
				stored.update(editor);
			};
			var publishTextChange = function(previous:String) {
				updateState();
				if (previous != value && onChange != null)
					onChange(value);
			};
			var syncCursor = function(geometry:ResolvedLayoutItem) {
				if (!editor.focused || context.platformSurface == null || context.platformSurface.isDisposed())
					return;
				var caret = editor.layout.caret(new TextPosition(editor.selectionFocus, 0));
				var topX = geometry.x + caret.x + caret.ascender * caret.slope;
				var topY = geometry.y + caret.y + caret.ascender;
				var bottomX = geometry.x + caret.x + caret.descender * caret.slope;
				var bottomY = geometry.y + caret.y + caret.descender;
				var transform:Transform2D = cast geometry.transform;
				var screenTopX = transform.a * topX + transform.c * topY + transform.tx;
				var screenTopY = transform.b * topX + transform.d * topY + transform.ty;
				var screenBottomX = transform.a * bottomX + transform.c * bottomY + transform.tx;
				var screenBottomY = transform.b * bottomX + transform.d * bottomY + transform.ty;
				context.textInput.update(editor.layoutText(), Utf8Text.length(editor.text),
					editor.selectionStart, editor.selectionEnd, editor.compositionStart,
					editor.compositionEnd, 0, 0,
					new Rect(Math.min(screenTopX, screenBottomX), Math.min(screenTopY, screenBottomY),
						Math.max(1.0, absolute(screenBottomX - screenTopX)),
						Math.max(1.0, absolute(screenBottomY - screenTopY))));
			};
			textNode.onResolved(function(geometry) {
				editor.updateLayout(geometry.width);
				syncCursor(geometry);
			});
			textNode.onPaint(function(canvas, _) {
				if (!editor.isDisposed())
					paintEditor(canvas, editor);
			});

			node.on(UiEventKind.Focus, function(_) {
				if (!enabled)
					return;
				editor.focused = true;
				semantics.states |= AccessibilityState.Focused;
				stored.update(editor);
				context.textInput.activate();
				if (textNode.resolved != null)
					syncCursor(cast textNode.resolved);
			});
			var blur = function(event:UiEvent) {
				if (!editor.focused && !editor.draggingSelection)
					return;
				editor.focused = false;
				editor.draggingSelection = false;
				semantics.states &= ~AccessibilityState.Focused;
				stored.update(editor);
				context.textInput.deactivate();
			};
			node.on(UiEventKind.Blur, blur);
			node.on(UiEventKind.FocusLost, blur);

			node.on(UiEventKind.PointerDown, function(event) {
				if (!enabled || event.button != 0 || textNode.resolved == null)
					return;
				var geometry:ResolvedLayoutItem = cast textNode.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				var offset = editor.hitTest(point.x - geometry.x, point.y - geometry.y);
				if (editor.placeCaret(offset, (event.modifiers & UiModifier.Shift) != 0))
					updateState();
				editor.draggingSelection = true;
				event.preventDefault();
			});
			node.on(UiEventKind.PointerMove, function(event) {
				if (!enabled || !editor.draggingSelection || textNode.resolved == null)
					return;
				var geometry:ResolvedLayoutItem = cast textNode.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				if (editor.placeCaret(editor.hitTest(point.x - geometry.x, point.y - geometry.y), true))
					updateState();
			});
			node.on(UiEventKind.PointerUp, function(_) { editor.draggingSelection = false; });
			node.on(UiEventKind.PointerCancel, function(_) { editor.draggingSelection = false; });

			var handleKey = function(event:UiEvent) {
				if (!enabled)
					return;
				var extend = (event.modifiers & UiModifier.Shift) != 0;
				var command = (event.modifiers & (UiModifier.Control | UiModifier.Super)) != 0;
				var handled = true;
				var changed = false;
				var previousText = editor.layoutText();
				if (command && event.key == UiKey.A)
					changed = editor.selectAll();
				else if (command && event.key == UiKey.C)
					copySelection(context.clipboard, editor);
				else if (command && event.key == UiKey.X) {
					copySelection(context.clipboard, editor);
					changed = editor.replace(editor.selectionStart, editor.selectionEnd, "");
				} else if (command && event.key == UiKey.V) {
					context.clipboard.readText(function(pasted) {
						if (editor.isDisposed() || !editor.focused)
							return;
						var beforePaste = editor.layoutText();
						if (editor.insert(pasted))
							publishTextChange(beforePaste);
					});
				} else if (event.key == UiKey.Left)
					changed = editor.moveCaret(-1, extend);
				else if (event.key == UiKey.Right)
					changed = editor.moveCaret(1, extend);
				else if (event.key == UiKey.Home)
					changed = editor.placeCaret(0, extend);
				else if (event.key == UiKey.End)
					changed = editor.placeCaret(Utf8Text.length(editor.text), extend);
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
				if (handled)
					event.preventDefault();
			};
			node.on(UiEventKind.KeyDown, handleKey);
			node.on(UiEventKind.KeyRepeat, handleKey);

			node.on(UiEventKind.TextInput, function(event) {
				var previousText = editor.layoutText();
				if (enabled && editor.insert(event.text))
					publishTextChange(previousText);
			});
			node.on(UiEventKind.TextEdit, function(event) {
				if (!enabled || event.data == null)
					return;
				var edit:NativeKitTextEdit = cast event.data;
				var previousText = editor.layoutText();
				if (editor.applyTextEdit(edit))
					publishTextChange(previousText);
			});
			node.on(UiEventKind.AccessibilitySetValue, function(event) {
				var previousText = editor.layoutText();
				if (enabled && editor.replace(0, Utf8Text.length(editor.text), event.text))
					publishTextChange(previousText);
			});
			node.on(UiEventKind.AccessibilitySetSelection, function(event) {
				if (!enabled || event.data == null)
					return;
				var request:AccessibilityActionData = cast event.data;
				if (editor.setSelection(request.selectionStart, request.selectionEnd))
					updateState();
			});
			return node;
		});
	}

	static function acquireState(context:BuildContext, id:nativekit.ui.core.WidgetId,
			value:String, textStyle:TextStyle, multiline:Bool):State<TextEditorState> {
		if (context.stateStore.contains(id))
			return context.existingState(id);
		if (context.fonts == null || context.fonts.isDisposed())
			throw "Text fields require fonts on their build context";
		var paragraph = new ParagraphStyle();
		paragraph.wrap = multiline ? TextWrap.WordCharacter : TextWrap.None;
		var editor = new TextEditorState(cast context.fonts, value, textStyle, paragraph);
		var stored:State<TextEditorState> = cast context.state(id, editor);
		var owned:TextEditorState = editor;
		context.stateStore.onDispose(id, function() { owned.dispose(); });
		return stored;
	}

	static function copySelection(clipboard:nativekit.ui.core.ClipboardService,
			editor:TextEditorState):Void {
		if (editor.selectionStart != editor.selectionEnd)
			clipboard.writeText(Utf8Text.slice(editor.layoutText(), editor.selectionStart,
				editor.selectionEnd));
	}

	static function paintEditor(canvas:Canvas, editor:TextEditorState):Void {
		if (editor.selectionStart != editor.selectionEnd) {
			for (rect in editor.layout.selectionRects(new TextPosition(editor.selectionStart, 0),
					new TextPosition(editor.selectionEnd, 0)))
				canvas.fillRect(rect, Color.rgba(0.2, 0.43, 0.82, 0.55));
		}
		if (editor.compositionStart >= 0 && editor.compositionStart != editor.compositionEnd) {
			for (rect in editor.layout.selectionRects(new TextPosition(editor.compositionStart, 0),
					new TextPosition(editor.compositionEnd, 0)))
				canvas.fillRect(new Rect(rect.x, rect.y + rect.height - 1.0, rect.width, 1.0),
					Color.rgba(0.95, 0.75, 0.24, 1.0));
		}
		if (editor.focused && editor.selectionStart == editor.selectionEnd) {
			var caret = editor.layout.caret(new TextPosition(editor.selectionFocus, 0));
			var topX = caret.x + caret.ascender * caret.slope;
			var topY = caret.y + caret.ascender;
			var bottomX = caret.x + caret.descender * caret.slope;
			var bottomY = caret.y + caret.descender;
			var x = Math.min(topX, bottomX);
			var y = Math.min(topY, bottomY);
			var height = Math.max(1.0, absolute(bottomY - topY));
			canvas.fillRect(new Rect(x, y, Math.max(1.0, absolute(bottomX - topX)), height),
				Color.rgba(0.96, 0.97, 0.99, 1.0));
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
