package nativekit.ui.widgets;

/** Semantic editing commands independent of physical keys and shortcuts. */
enum TextEditorCommand {
	MoveGraphemeBackward;
	MoveGraphemeForward;
	MoveWordBackward;
	MoveWordForward;
	MoveParagraphBackward;
	MoveParagraphForward;
	MoveVisualLineUp;
	MoveVisualLineDown;
	MoveLineStart;
	MoveLineEnd;
	MoveDocumentStart;
	MoveDocumentEnd;
	DeleteGraphemeBackward;
	DeleteGraphemeForward;
	DeleteWordBackward;
	DeleteWordForward;
	SelectAll;
	CopySelection;
	CutSelection;
	Paste;
	InsertNewline;
	Submit;
}
