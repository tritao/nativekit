package nativekit.ui.widgets;

import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;

/**
 * Derives the default history grouping for a platform edit event.
 *
 * Platform adapters report ranges, not editor commands.  Keeping this small
 * policy next to the editor means both TextEditorState and TextInputBridge
 * apply the same grouping rules without teaching platform code about the
 * editor's history implementation.
 */
class TextEditorHistoryPolicy {
	public static function forNativeEdit(edit:NativeKitTextEdit,
			currentStart:Int, currentEnd:Int):TextEditorHistoryKind {
		if (edit == null)
			return TextEditorHistoryKind.Generic;
		if (edit.historyKind != 0) {
			return switch (edit.historyKind) {
				case 1: TextEditorHistoryKind.Typing;
				case 2: TextEditorHistoryKind.DeleteBackward;
				case 3: TextEditorHistoryKind.DeleteForward;
				case 4: TextEditorHistoryKind.Paste;
				case 5: TextEditorHistoryKind.Autocorrect;
				case 6: TextEditorHistoryKind.Composition;
				case _: TextEditorHistoryKind.Generic;
			};
		}
		switch (edit.action) {
			case TextEditAction.Compose:
				return TextEditorHistoryKind.Composition;
			case TextEditAction.Commit:
				return currentStart == currentEnd && edit.replaceStart == currentStart &&
					edit.replaceEnd == currentEnd ? TextEditorHistoryKind.Typing :
					TextEditorHistoryKind.Generic;
			case TextEditAction.Delete:
				if (currentStart == currentEnd) {
					if (edit.replaceEnd == currentStart)
						return TextEditorHistoryKind.DeleteBackward;
					if (edit.replaceStart == currentStart)
						return TextEditorHistoryKind.DeleteForward;
				}
				return TextEditorHistoryKind.Generic;
			case _:
				return TextEditorHistoryKind.Generic;
		}
		return TextEditorHistoryKind.Generic;
	}
}
