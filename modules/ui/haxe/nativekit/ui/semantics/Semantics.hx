package nativekit.ui.semantics;

/** Haxe-owned accessible name, value, role, state, and supported actions. */
class Semantics {
	public final role:AccessibilityRole;
	public var label:Null<String>;
	public var value:Null<String>;
	public var states:Int;
	public var actions:Int;
	public var numericValue:Float;
	public var numericMinimum:Float;
	public var numericMaximum:Float;
	public var textStart:Int;
	public var documentLength:Int;
	public var selectionStart:Int;
	public var selectionEnd:Int;
	public var setSize:Int;
	public var positionInSet:Int;
	public var rowCount:Int;
	public var columnCount:Int;
	/** Zero-based row index; -1 means unspecified. */
	public var rowIndex:Int;
	/** Zero-based column index; -1 means unspecified. */
	public var columnIndex:Int;
	public var rowSpan:Int;
	public var columnSpan:Int;
	/** One-based hierarchy depth; zero means unspecified. */
	public var hierarchyLevel:Int;
	public var orientation:AccessibilityOrientation;

	public function new(role:AccessibilityRole, ?label:String, ?value:String) {
		this.role = role;
		this.label = label;
		this.value = value;
		states = 0;
		actions = 0;
		numericValue = 0.0;
		numericMinimum = 0.0;
		numericMaximum = 0.0;
		textStart = 0;
		documentLength = 0;
		selectionStart = -1;
		selectionEnd = -1;
		setSize = 0;
		positionInSet = 0;
		rowCount = 0;
		columnCount = 0;
		rowIndex = -1;
		columnIndex = -1;
		rowSpan = 0;
		columnSpan = 0;
		hierarchyLevel = 0;
		orientation = AccessibilityOrientation.Unspecified;
	}
}
