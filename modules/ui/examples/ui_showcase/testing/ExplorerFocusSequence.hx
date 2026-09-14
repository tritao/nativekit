package testing;

import UiExplorer;
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
		var targetLabel = state.visualFocusLabel;
		state.visualFocusLabel = null;
		var selectTextArea = state.visualTextAreaSelection;
		state.visualTextAreaSelection = false;
		var focused = false;
		for (record in explorer.context.inspect())
			if (!focused && ((targetLabel != null && record.label == targetLabel) ||
				(targetLabel == null && record.role == AccessibilityRole.TextField))) {
				var widgetId = new WidgetId(record.id);
				focused = explorer.context.focusWidget(widgetId);
				if (focused && selectTextArea) {
					var editorState:State<TextEditorState> =
						explorer.context.buildContext.existingState(widgetId);
					var editor:TextEditorState = cast editorState.value;
					if (!editor.setSelection(6, 15))
						throw "UI visual test could not select TextArea text";
					editorState.update(editor);
				}
			}
		if (!focused)
			throw "UI smoke test could not focus a TextField";
	}
}
