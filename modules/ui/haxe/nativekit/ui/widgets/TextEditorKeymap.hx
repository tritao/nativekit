package nativekit.ui.widgets;

import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;

/** Converts a physical key plus platform modifiers into an editor command. */
class TextEditorKeymap {
	public static function commandForKey(key:Int, modifiers:Int, multiline:Bool,
			macStyle:Bool):Null<TextEditorCommand> {
		var control = (modifiers & UiModifier.Control) != 0;
		var alt = (modifiers & UiModifier.Alt) != 0;
		var superKey = (modifiers & UiModifier.Super) != 0;
		var commandModifier = control || superKey;
		var wordModifier = macStyle ? (alt && !control && !superKey) : control;

		if (commandModifier && key == UiKey.A)
			return TextEditorCommand.SelectAll;
		if (commandModifier && key == UiKey.C)
			return TextEditorCommand.CopySelection;
		if (commandModifier && key == UiKey.X)
			return TextEditorCommand.CutSelection;
		if (commandModifier && key == UiKey.V)
			return TextEditorCommand.Paste;

		switch (key) {
			case UiKey.Left:
				if (macStyle && superKey)
					return TextEditorCommand.MoveDocumentStart;
				if (wordModifier)
					return TextEditorCommand.MoveWordBackward;
				return TextEditorCommand.MoveGraphemeBackward;
			case UiKey.Right:
				if (macStyle && superKey)
					return TextEditorCommand.MoveDocumentEnd;
				if (wordModifier)
					return TextEditorCommand.MoveWordForward;
				return TextEditorCommand.MoveGraphemeForward;
			case UiKey.Up:
				if (macStyle && superKey)
					return TextEditorCommand.MoveDocumentStart;
				if (wordModifier)
					return TextEditorCommand.MoveParagraphBackward;
				return multiline ? TextEditorCommand.MoveVisualLineUp : null;
			case UiKey.Down:
				if (macStyle && superKey)
					return TextEditorCommand.MoveDocumentEnd;
				if (wordModifier)
					return TextEditorCommand.MoveParagraphForward;
				return multiline ? TextEditorCommand.MoveVisualLineDown : null;
			case UiKey.Home:
				return commandModifier ? TextEditorCommand.MoveDocumentStart :
					multiline ? TextEditorCommand.MoveLineStart : TextEditorCommand.MoveDocumentStart;
			case UiKey.End:
				return commandModifier ? TextEditorCommand.MoveDocumentEnd :
					multiline ? TextEditorCommand.MoveLineEnd : TextEditorCommand.MoveDocumentEnd;
			case UiKey.Backspace:
				return wordModifier ? TextEditorCommand.DeleteWordBackward :
					TextEditorCommand.DeleteGraphemeBackward;
			case UiKey.Delete:
				return wordModifier ? TextEditorCommand.DeleteWordForward :
					TextEditorCommand.DeleteGraphemeForward;
			case UiKey.Enter:
				return multiline ? TextEditorCommand.InsertNewline : TextEditorCommand.Submit;
			case _:
				return null;
		}
		return null;
	}
}
