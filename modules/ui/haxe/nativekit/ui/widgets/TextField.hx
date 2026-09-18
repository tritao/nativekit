package nativekit.ui.widgets;

import Canvas;
import Color;
import Insets;
import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import NativeKit.TextInputAction;
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
import nativekit.ui.core.TextInputBridge;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
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
import nativekit.ui.widgets.TextEditorCommand;
import nativekit.ui.widgets.TextEditorKeymap;

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
			var document:TextDocumentEngine = editor.documentEngine;
			editor.updateStyle(resolved.textStyle, resolved.paragraphStyle);
			if (editor.syncExternal(value))
				editor.resetCaretBlink(context.gestures.timeSeconds());
			var documentText = document.text();
			var documentSelection = document.selection();
			var documentComposition = document.composition();

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
				label == null ? key : label, documentText);
			semantics.actions = semanticActions;
			semantics.textStart = 0;
			semantics.documentLength = document.documentLength();
			semantics.selectionStart = documentSelection.start;
			semantics.selectionEnd = documentSelection.end;
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
					paintSelection(canvas, document, editor, context.textInput.isOwner(id), context.theme);
			});
			editorContent.add(selectionNode);
			var showsPlaceholder = documentText.length == 0 && placeholder != null &&
				placeholder.length > 0;
			var textNode = new RenderNode(context.id("text"),
				showsPlaceholder ? LayoutVisualKind.Text : LayoutVisualKind.Custom, textNodeStyle);
			if (showsPlaceholder)
				textNode.layout.text = placeholder;
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
			if (!showsPlaceholder) {
				editor.updateStyle(textNodeTextStyle, editor.paragraphStyle);
				editor.setRenderColor(textNodeColor);
				textNode.layout.intrinsicContent = editor.renderContent;
			}
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
					paintEditorDecorations(canvas, document, editor, context.textInput.isOwner(id),
						context.theme, context.gestures.timeSeconds());
			});
			editorContent.add(paintNode);
			node.add(editorContent);

			var updateState = function() {
				documentText = document.text();
				documentSelection = document.selection();
				documentComposition = document.composition();
				value = documentText;
				semantics.value = value;
				semantics.documentLength = document.documentLength();
				semantics.selectionStart = documentSelection.start;
				semantics.selectionEnd = documentSelection.end;
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
				var diagnostics = onDiagnostics;
				if (diagnostics == null)
					return;
				var compositionText = "";
				var diagnosticsCompositionRange = documentComposition.range;
				if (diagnosticsCompositionRange != null) {
					var offsets = new TextOffsetMap(documentText);
					compositionText = offsets.sliceCodepoints(diagnosticsCompositionRange.start,
						diagnosticsCompositionRange.end);
				}
				diagnostics(new TextEditorDiagnostics(key, label == null ? key : label,
					editor.focused, documentSelection.start, documentSelection.end,
					documentSelection.focus,
					diagnosticsCompositionRange == null ? -1 : diagnosticsCompositionRange.start,
					diagnosticsCompositionRange == null ? -1 : diagnosticsCompositionRange.end,
					compositionText, caretRect, context.textInput.platformSupported,
					context.textInput.platformActive));
			};
			var syncCursor = function(geometry:ResolvedLayoutItem) {
				var scrollDelta = multiline ? editor.scrollOffsetY - builtScrollOffset : 0.0;
				var transform:Transform2D = cast geometry.transform;
				var caretResult = document.layout(new TextRange(documentSelection.focus,
					documentSelection.focus));
				var caret = caretResult.caret;
				if (caret == null) {
					publishDiagnostics(null);
					return;
				}
				var topX = geometry.x + caret.x + caret.ascender * caret.slope;
				var topY = geometry.y + caret.y + caret.ascender;
				var bottomX = geometry.x + caret.x + caret.descender * caret.slope;
				var bottomY = geometry.y + caret.y + caret.descender;
				var screenTopX = transform.a * topX + transform.c * topY + transform.tx;
				var screenTopY = transform.b * topX + transform.d * topY + transform.ty;
				var screenBottomX = transform.a * bottomX + transform.c * bottomY + transform.tx;
				var screenBottomY = transform.b * bottomX + transform.d * bottomY + transform.ty;
				if (multiline) {
					// Resolution callbacks can reveal the caret after the node's
					// transform was built. Keep the same-frame IME anchor in sync.
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
				var selectionGeometry:Array<Rect> = [];
				var selectionRangeGeometry:Array<TextRangeRect> = [];
				if (!documentSelection.isCollapsed()) {
					var selectionLayout = document.layout(new TextRange(documentSelection.start,
						documentSelection.end));
					for (rect in selectionLayout.rects) {
						var geometryRect = new Rect(rect.x, rect.y, rect.width, rect.height);
						selectionGeometry.push(transformTextRect(geometryRect, geometry, transform, scrollDelta));
						selectionRangeGeometry.push(transformTextRangeRect(rect, geometry, transform, scrollDelta));
					}
				}
				var compositionGeometry:Array<Rect> = [];
				var compositionRangeGeometry:Array<TextRangeRect> = [];
				if (documentComposition.isActive()) {
					var compositionRange = documentComposition.range;
					if (compositionRange != null) {
						var compositionLayout = document.layout(compositionRange);
						for (rect in compositionLayout.rects) {
							var geometryRect = new Rect(rect.x, rect.y, rect.width, rect.height);
							compositionGeometry.push(transformTextRect(geometryRect, geometry, transform, scrollDelta));
							compositionRangeGeometry.push(transformTextRangeRect(rect, geometry, transform, scrollDelta));
						}
					}
				}
				context.textInput.update(document, 0,
					multiline ? 1 : 0,
					caretRect, selectionGeometry, compositionGeometry, selectionRangeGeometry,
					compositionRangeGeometry);
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
				if (editor.isDisposed())
					return;
				if (!editor.focused && !editor.draggingSelection)
					return;
				// A platform may deliver focus loss before its final IME commit. Keep
				// the preedit in the document and drop only composition metadata.
				commitDocumentComposition(document);
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
				if (document.composition().isActive()) {
					commitDocumentComposition(document);
					updateState();
				}
				event.capturePointer();
				var geometry:ResolvedLayoutItem = cast textNode.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				var position = document.hitTest(new TextPoint(point.x - geometry.x, point.y - geometry.y));
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
				var position = document.hitTest(new TextPoint(point.x - geometry.x, point.y - geometry.y));
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
				var macStyle = #if (mac || ios) true #else false #end;
				var command = TextEditorKeymap.commandForKey(event.key, event.modifiers,
					multiline, macStyle);
				var handled = command != null;
				var changed = false;
				if (handled && document.composition().isActive()) {
					commitDocumentComposition(document);
					updateState();
				}
				var previousText = document.text();
				if (command == TextEditorCommand.CopySelection)
					copySelection(context.clipboard, document);
				else if (command == TextEditorCommand.CutSelection) {
					copySelection(context.clipboard, document);
					changed = applyDeleteSelection(document, TextEditorHistoryKind.Generic);
				} else if (command == TextEditorCommand.Paste) {
					context.clipboard.readText(function(pasted) {
						if (editor.isDisposed() || !editor.focused)
							return;
						var beforePaste = document.text();
						if (applyInsert(document, pasted, TextEditorHistoryKind.Paste)) {
							editor.resetCaretBlink(context.gestures.timeSeconds());
							publishTextChange(beforePaste);
						}
					});
				} else if (command == TextEditorCommand.Submit) {
					if (onSubmit != null)
						onSubmit(document.text());
				} else if (command == TextEditorCommand.Undo) {
					changed = document.undo();
				} else if (command == TextEditorCommand.Redo) {
					changed = document.redo();
				} else if (command != null)
					changed = editor.executeCommand(command, extend, macStyle);
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
				var previousText = document.text();
				if (enabled && applyInsert(document, event.text, TextEditorHistoryKind.Typing)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.TextEdit, function(event) {
				if (!enabled || event.data == null)
					return;
				var edit:NativeKitTextEdit = cast event.data;
				var previousText = document.text();
				var transaction = TextInputBridge.transactionForEdit(edit, document);
				if (transaction != null && applyDocumentTransaction(document, transaction)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.TextAction, function(event) {
				if (!enabled || event.data == null)
					return;
				var action:TextInputAction = cast event.data;
				if (action == TextInputAction.Next)
					context.focusNext();
				else if (onSubmit != null && (action == TextInputAction.Default ||
					action == TextInputAction.Done || action == TextInputAction.Go ||
					action == TextInputAction.Search || action == TextInputAction.Send))
					onSubmit(document.text());
			});
			node.on(UiEventKind.AccessibilitySetValue, function(event) {
				var previousText = document.text();
				if (enabled && applyReplaceDocument(document, event.text)) {
					editor.resetCaretBlink(context.gestures.timeSeconds());
					publishTextChange(previousText);
				}
			});
			node.on(UiEventKind.AccessibilitySetSelection, function(event) {
				if (!enabled || event.data == null)
					return;
				var request:AccessibilityActionData = cast event.data;
				if (applySelection(document, request.selectionStart, request.selectionEnd)) {
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

	static function applyInsert(document:TextDocumentEngine, value:Null<String>,
			historyKind:TextEditorHistoryKind):Bool {
		if (document == null || value == null || value.length == 0)
			return false;
		var selection = document.selection();
		var actualKind = historyKind;
		if (actualKind == TextEditorHistoryKind.Typing && value.indexOf("\n") >= 0)
			actualKind = TextEditorHistoryKind.Generic;
		var count = TextOffsetMap.countCodepoints(value);
		return applyDocumentTransaction(document, new EditTransaction(selection.start, selection.end,
			value, selection.start + count, selection.start + count, false, -1, -1, 0, null,
			actualKind));
	}

	static function applyReplaceDocument(document:TextDocumentEngine, value:Null<String>):Bool {
		var replacement = value == null ? "" : value;
		var count = TextOffsetMap.countCodepoints(replacement);
		return applyDocumentTransaction(document, new EditTransaction(0,
			document.documentLength(), replacement, count, count));
	}

	static function applyDeleteSelection(document:TextDocumentEngine,
			historyKind:TextEditorHistoryKind):Bool {
		var selection = document.selection();
		if (selection.isCollapsed())
			return false;
		return applyDocumentTransaction(document, new EditTransaction(selection.start, selection.end, "",
			selection.start, selection.start, false, -1, -1, 0, null, historyKind));
	}

	static function applySelection(document:TextDocumentEngine, start:Int, end:Int):Bool {
		var selection = document.selection();
		var composition = document.composition();
		var compositionRange = composition.range;
		var hasComposition = compositionRange != null;
		var compositionStart = -1;
		var compositionEnd = -1;
		if (compositionRange != null) {
			compositionStart = compositionRange.start;
			compositionEnd = compositionRange.end;
		}
		return applyDocumentTransaction(document, new EditTransaction(selection.start, selection.start, "",
			start, end, hasComposition, compositionStart, compositionEnd));
	}

	static function commitDocumentComposition(document:TextDocumentEngine):Bool {
		return document.commitComposition();
	}

	static function applyDocumentTransaction(document:TextDocumentEngine,
			transaction:EditTransaction):Bool {
		if (document == null || transaction == null)
			return false;
		var beforeText = document.text();
		var beforeSelection = document.selection();
		var beforeComposition = document.composition();
		document.applyEdit(transaction);
		var afterSelection = document.selection();
		var afterComposition = document.composition();
		return beforeText != document.text() || !sameSelection(beforeSelection, afterSelection) ||
			!sameComposition(beforeComposition, afterComposition);
	}

	static function sameSelection(first:SelectionState, second:SelectionState):Bool {
		return first.start == second.start && first.end == second.end &&
			first.anchor == second.anchor && first.focus == second.focus &&
			first.anchorAffinity == second.anchorAffinity &&
			first.focusAffinity == second.focusAffinity;
	}

	static function sameComposition(first:CompositionState, second:CompositionState):Bool {
		if (first.range == null || second.range == null)
			return first.range == null && second.range == null &&
				first.attributes.length == second.attributes.length;
		if (first.range.start != second.range.start || first.range.end != second.range.end ||
			first.attributes.length != second.attributes.length)
			return false;
		for (index in 0...first.attributes.length) {
			var left = first.attributes[index];
			var right = second.attributes[index];
			if (left.start != right.start || left.end != right.end ||
				left.selected != right.selected || left.target != right.target)
				return false;
		}
		return true;
	}

	static function copySelection(clipboard:nativekit.ui.core.ClipboardService,
			document:TextDocumentEngine):Void {
		var selection = document.selection();
		if (!selection.isCollapsed())
			clipboard.writeText(new TextOffsetMap(document.text()).sliceCodepoints(selection.start,
				selection.end));
	}

	static function paintSelection(canvas:Canvas, document:TextDocumentEngine,
			editor:TextEditorState,
			active:Bool, theme:nativekit.ui.theme.Theme):Void {
		var selection = document.selection();
		if (!selection.isCollapsed()) {
			canvas.translate(0.0, -editor.scrollOffsetY);
			for (rect in document.layout(new TextRange(selection.start, selection.end)).rects)
				canvas.fillRectIfPositive(new Rect(rect.x, rect.y, rect.width, rect.height),
					active ? theme.textSelection : theme.textSelectionInactive);
		}
	}

	static function paintEditorDecorations(canvas:Canvas, document:TextDocumentEngine,
			editor:TextEditorState,
			active:Bool, theme:nativekit.ui.theme.Theme, timeSeconds:Float):Void {
		if (editor.scrollOffsetY != 0.0)
			canvas.translate(0.0, -editor.scrollOffsetY);
		var composition = document.composition();
		if (active && composition.isActive()) {
			for (attribute in composition.attributes) {
				var color = attribute.selected ? theme.textCompositionSelected :
					attribute.target ? theme.textCompositionTarget : theme.textComposition;
				for (rect in document.layout(new TextRange(attribute.start, attribute.end)).rects)
					canvas.fillRectIfPositive(new Rect(rect.x, rect.y + rect.height - 1.0,
						rect.width, 1.0), color);
			}
		}
		var selection = document.selection();
		if (active && selection.isCollapsed() &&
				editor.isCaretVisible(timeSeconds)) {
			var caret = document.layout(new TextRange(selection.focus, selection.focus)).caret;
			if (caret == null)
				return;
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

	static function transformTextRect(rect:Rect, geometry:ResolvedLayoutItem,
			transform:Transform2D, scrollDelta:Float):Rect {
		var left = geometry.x + rect.x;
		var top = geometry.y + rect.y - scrollDelta;
		var right = left + rect.width;
		var bottom = top + rect.height;
		var x0 = transform.a * left + transform.c * top + transform.tx;
		var y0 = transform.b * left + transform.d * top + transform.ty;
		var x1 = transform.a * right + transform.c * top + transform.tx;
		var y1 = transform.b * right + transform.d * top + transform.ty;
		var x2 = transform.a * left + transform.c * bottom + transform.tx;
		var y2 = transform.b * left + transform.d * bottom + transform.ty;
		var x3 = transform.a * right + transform.c * bottom + transform.tx;
		var y3 = transform.b * right + transform.d * bottom + transform.ty;
		var minX = Math.min(Math.min(x0, x1), Math.min(x2, x3));
		var minY = Math.min(Math.min(y0, y1), Math.min(y2, y3));
		var maxX = Math.max(Math.max(x0, x1), Math.max(x2, x3));
		var maxY = Math.max(Math.max(y0, y1), Math.max(y2, y3));
		return new Rect(minX, minY, Math.max(0.0, maxX - minX),
			Math.max(0.0, maxY - minY));
	}

	static function transformTextRangeRect(rect:TextRangeRect, geometry:ResolvedLayoutItem,
			transform:Transform2D, scrollDelta:Float):TextRangeRect {
		var transformed = transformTextRect(new Rect(rect.x, rect.y, rect.width, rect.height),
			geometry, transform, scrollDelta);
		return new TextRangeRect(rect.start, rect.end, transformed.x, transformed.y,
			transformed.width, transformed.height);
	}

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
