package pages;

import UiExplorer;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Text editing, multilingual shaping, selection and platform IME status. */
class TextPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Text & Input",
			"Native text shaping, selection and keyboard editing, with platform IME composition where available.");
		items.push(explorer.keyed("text-input-row", new Row("text-input-row", [
			explorer.keyed("single-line", explorer.panel("text-field-card", [
				explorer.keyed("heading", explorer.text("TextField", explorer.paletteText())),
				explorer.keyed("copy", explorer.text("Type, select, paste and move the caret with the keyboard.", explorer.paletteMuted())),
				explorer.keyed("field", explorer.textField()),
				explorer.keyed("value", explorer.text('Current value: ${explorer.state.controls.nameValue}', explorer.paletteMuted()))
			])),
			explorer.keyed("multiline", explorer.panel("text-area-card", [
				explorer.keyed("heading", explorer.text("TextArea + IME", explorer.paletteText())),
				explorer.keyed("copy", explorer.text("Compose accented text or switch to an RTL / CJK keyboard.", explorer.paletteMuted())),
				explorer.keyed("area", explorer.textArea()),
				explorer.keyed("sample", explorer.text("مرحبا NativeKit  ·  שלום  ·  こんにちは  ·  👋",
					UiExplorer.color(0.40, 0.83, 0.87)))
			]))
		], explorer.rowStyle(14.0))));
		items.push(explorer.keyed("text-architecture", explorer.panel("text-architecture", [
			explorer.keyed("heading", explorer.text("Platform text-input status", explorer.paletteText())),
			explorer.keyed("copy", explorer.text(textInputStatus(explorer), explorer.paletteMuted()))
		])));
	}

	static function textInputStatus(explorer:UiExplorer):String {
		if (explorer.context.textInput.platformChecked && !explorer.context.textInput.platformSupported)
			return "This NativeKit backend does not provide custom IME state. The editor remains active for delivered Unicode text input, but composition updates may be unavailable.";
		if (explorer.context.textInput.platformSupported)
			return "The focused editor publishes selection, composition range and caret bounds to NativeKit; the host returns committed text and composition updates through the UI event path.";
		return "Focus a text field to check custom IME support. Unsupported backends keep text editing active, while composition updates may be unavailable.";
	}
}
