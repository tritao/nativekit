package pages;

import Insets;
import LayoutAxis;
import LayoutStyle;
import TextStyle;
import UiExplorer;
import nativekit.ui.core.UiKey;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.TextArea;
import nativekit.ui.widgets.TextEditorDiagnostics;
import nativekit.ui.widgets.TextField;

/** Interactive text editing, multilingual shaping, selection and IME diagnostics. */
class TextPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Text & Input",
			"Edit real text, exercise clipboard and focus traversal, and inspect the live IME transaction.");

		items.push(explorer.keyed("text-editors", new Row("text-editors", [
			explorer.keyed("latin-and-rtl", explorer.panel("latin-and-rtl-card", [
				explorer.keyed("heading", explorer.text("Latin, Arabic & Hebrew", explorer.paletteText())),
				explorer.keyed("latin-label", explorer.text("Display name · Enter submits", explorer.paletteMuted())),
				explorer.keyed("latin", editor(explorer, "demo-name", explorer.state.controls.nameValue,
				"Display name", false, function(value) {
					explorer.state.controls.nameValue = value;
				})),
				explorer.keyed("arabic-label", explorer.text("Arabic", explorer.paletteMuted())),
				explorer.keyed("arabic", editor(explorer, "text-arabic", explorer.state.textArabicValue,
				"Arabic", false, function(value) { explorer.state.textArabicValue = value; })),
				explorer.keyed("hebrew-label", explorer.text("Hebrew", explorer.paletteMuted())),
				explorer.keyed("hebrew", editor(explorer, "text-hebrew", explorer.state.textHebrewValue,
				"Hebrew", false, function(value) { explorer.state.textHebrewValue = value; }))
			])),
			explorer.keyed("cjk-and-emoji", explorer.panel("cjk-and-emoji-card", [
				explorer.keyed("heading", explorer.text("Japanese, emoji & multiline", explorer.paletteText())),
				explorer.keyed("japanese-label", explorer.text("Japanese", explorer.paletteMuted())),
				explorer.keyed("japanese", editor(explorer, "text-japanese", explorer.state.textJapaneseValue,
				"Japanese", false, function(value) { explorer.state.textJapaneseValue = value; })),
				explorer.keyed("emoji-label", explorer.text("Emoji", explorer.paletteMuted())),
				explorer.keyed("emoji", editor(explorer, "text-emoji", explorer.state.textEmojiValue,
				"Emoji", false, function(value) { explorer.state.textEmojiValue = value; })),
				explorer.keyed("notes-label", explorer.text("Multilingual notes · ↑ / ↓ and Home / End are line-aware", explorer.paletteMuted())),
				explorer.keyed("notes", editor(explorer, "demo-notes", explorer.state.controls.notesValue,
				"Multilingual notes", true, function(value) {
					explorer.state.controls.notesValue = value;
				}))
			])),
		], explorer.rowStyle(14.0))));

		items.push(explorer.keyed("text-actions", explorer.panel("text-actions-card", [
			explorer.keyed("heading", explorer.text("Clipboard & focus tests", explorer.paletteText())),
			explorer.keyed("copy", explorer.text("The buttons focus Display name before invoking the same shortcuts used by the platform keyboard.", explorer.paletteMuted())),
			explorer.keyed("buttons", new Row("text-action-buttons", [
				explorer.keyed("copy-button", explorer.button("Copy", "text-copy", function() {
					if (explorer.textCommand("Display name", UiKey.C))
						explorer.recordTextClipboardAction("Copy requested from Display name");
				})),
				explorer.keyed("cut-button", explorer.button("Cut", "text-cut", function() {
					if (explorer.textCommand("Display name", UiKey.X))
						explorer.recordTextClipboardAction("Cut requested from Display name");
				})),
				explorer.keyed("paste-button", explorer.button("Paste", "text-paste", function() {
					if (explorer.textCommand("Display name", UiKey.V))
						explorer.recordTextClipboardAction("Paste requested into Display name");
				})),
				explorer.keyed("select-button", explorer.button("Select all", "text-select-all", function() {
					if (explorer.textCommand("Display name", UiKey.A))
						explorer.recordTextClipboardAction("Select all requested in Display name");
				}))
		], explorer.rowStyle(8.0))),
			explorer.keyed("focus-buttons", new Row("text-focus-buttons", [
				explorer.keyed("focus-latin", explorer.button("Focus Latin", "text-focus-latin", function() {
					explorer.focusTextEditor("Display name");
				})),
				explorer.keyed("focus-notes", explorer.button("Focus notes", "text-focus-notes", function() {
					explorer.focusTextEditor("Multilingual notes");
				}))
		], explorer.rowStyle(8.0))),
			explorer.keyed("tab-hint", explorer.text("Tab order: Display name → Arabic → Hebrew → Japanese → Emoji → Multilingual notes. Shift+Tab reverses it.", explorer.paletteMuted())),
			explorer.keyed("clipboard-status", explorer.text(explorer.state.textLastClipboardAction, explorer.paletteText())),
			explorer.keyed("submit-status", explorer.text("Submit: ${explorer.state.textLastSubmit}", explorer.paletteText()))
		])));

		items.push(explorer.keyed("text-diagnostics", explorer.panel("text-diagnostics-card", [
			explorer.keyed("heading", explorer.text("Live Text / IME diagnostics", explorer.paletteText())),
			explorer.keyed("copy", explorer.text(diagnosticsText(explorer), explorer.paletteMuted())),
			explorer.keyed("status", explorer.text(textInputStatus(explorer), explorer.paletteMuted()))
		])));
	}

	static function editor(explorer:UiExplorer, key:String, value:String, label:String,
			multiline:Bool, onChange:String->Void):TextField {
		var result:TextField;
		var style = editorStyle(explorer, multiline ? 142.0 : 42.0);
		if (multiline)
			result = new TextArea(key, value, onChange, style, label, new TextStyle(15.0), explorer.paletteText());
		else
			result = new TextField(key, value, onChange, style, label, new TextStyle(15.0), explorer.paletteText());
		result.onDiagnostics = explorer.recordTextDiagnostics;
		if (!multiline)
			result.onSubmit = function(_) { explorer.recordTextSubmit(label); };
		return result;
	}

	static function editorStyle(explorer:UiExplorer, height:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(height);
		style.padding = new Insets(11.0, 8.0, 11.0, 8.0);
		style.background = explorer.state.lightTheme
			? UiExplorer.color(0.92, 0.94, 0.98) : UiExplorer.color(0.09, 0.12, 0.18);
		style.radiusTopLeft = style.radiusTopRight = 5.0;
		style.radiusBottomLeft = style.radiusBottomRight = 5.0;
		return style;
	}

	static function diagnosticsText(explorer:UiExplorer):String {
		var diagnostics:Null<TextEditorDiagnostics> = explorer.state.textDiagnostics;
		if (diagnostics == null)
			return "Focused editor: none\nSelection: —\nCaret offset: —\nComposition: —\nPreedit text: —\nCaret screen rect: —";
		var composition = diagnostics.compositionStart < 0
			? "none"
			: '${diagnostics.compositionStart}..${diagnostics.compositionEnd}';
		var caret = diagnostics.caretRect;
		var caretRect = caret == null ? "unresolved" :
			'${Std.int(caret.x)}, ${Std.int(caret.y)} '
			+ '${Std.int(caret.width)} × ${Std.int(caret.height)}';
		return 'Focused editor: ${diagnostics.label}\n'
			+ 'Selection: ${diagnostics.selectionStart}..${diagnostics.selectionEnd}\n'
			+ 'Caret offset: ${diagnostics.caretOffset}\n'
			+ 'Composition: $composition\n'
			+ 'Preedit text: "${diagnostics.compositionText}"\n'
			+ 'Caret screen rect: $caretRect';
	}

	static function textInputStatus(explorer:UiExplorer):String {
		var diagnostics:Null<TextEditorDiagnostics> = explorer.state.textDiagnostics;
		if (diagnostics != null)
			return 'Platform IME: ${diagnostics.platformSupported ? "supported" : "unsupported"} / '
			+ '${diagnostics.platformActive ? "active" : "inactive"}';
		if (explorer.context.textInput.platformChecked && !explorer.context.textInput.platformSupported)
			return "Platform IME: unsupported / inactive. Unicode TextInput still edits the focused field.";
		if (explorer.context.textInput.platformSupported)
			return "Platform IME: supported; focus an editor to activate composition updates.";
		return "Platform IME: not checked. Focus an editor, then try a native composition keyboard.";
	}
}
