package nativekit.ui.widgets;

/**
 * Backend-neutral document/editor contract used by text-input integration.
 *
 * Positions in this interface are Unicode code-point offsets. Platform
 * adapters convert their native offsets before creating an EditTransaction;
 * an implementation may use any internal storage or layout engine.
 */
interface TextDocumentEngine {
	function applyEdit(transaction:EditTransaction):Void;

	function text():String;

	function documentLength():CodepointOffset;

	function selection():SelectionState;

	function composition():CompositionState;

	function commitComposition():Bool;

	function cancelComposition():Bool;

	function undo():Bool;

	function redo():Bool;

	function layout(range:TextRange):LayoutResult;

	function hitTest(point:TextPoint):TextPosition;
}
