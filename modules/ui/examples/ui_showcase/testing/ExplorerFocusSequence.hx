package testing;

import UiExplorer;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import nativekit.ui.core.State;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.widgets.TextEditorState;

/** Applies deterministic focus/selection requests after a page is submitted. */
class ExplorerFocusSequence {
	public static function applyAfterSubmit(explorer:UiExplorer):Void {
		var state = explorer.state;
		if (!state.smokeFocusTextField && state.visualFocusLabel == null)
			return;
		explorer.diagnosticStage = 4;
		state.smokeFocusTextField = false;
		// Visual cases keep their requested focus stable across browser capture frames.
		var targetLabel = state.visualFocusLabel;
		var selectTextArea = state.visualTextAreaSelection;
		var showComposition = state.visualTextComposition;
		state.visualTextAreaSelection = false;
		state.visualTextComposition = false;
		var focused = false;
		for (record in explorer.context.inspect())
			if (!focused && ((targetLabel != null && record.label == targetLabel) ||
				(targetLabel == null && record.role == AccessibilityRole.TextField))) {
				var widgetId = new WidgetId(record.id);
				focused = explorer.context.focusWidget(widgetId);
				if (focused && (selectTextArea || showComposition)) {
					var editorState:State<TextEditorState> =
						explorer.context.buildContext.existingState(widgetId);
					var editor:TextEditorState = cast editorState.value;
					if (selectTextArea && !editor.setSelection(6, 15))
						throw "UI visual test could not select TextArea text";
					if (showComposition) {
						editor.setSelection(15, 15);
						if (editor.selectionStart != 15 || editor.selectionEnd != 15)
							throw "UI visual test could not place composition caret";
						var composition = new NativeKitTextEdit(TextEditAction.SetComposition, null,
							0, 0, 15, 15, 6, 15);
						editor.applyTextEdit(composition);
						if (editor.compositionStart != 6 || editor.compositionEnd != 15)
							throw "UI visual test could not set composition range";
						}
					editorState.update(editor);
				}
			}
		if (!focused)
			throw "UI smoke test could not focus a TextField";
	}
}
