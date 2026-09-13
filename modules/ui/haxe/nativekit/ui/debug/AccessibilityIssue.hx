package nativekit.ui.debug;

/** One actionable accessibility audit finding. */
class AccessibilityIssue {
	public final nodeId:Int;
	public final code:String;
	public final message:String;

	public function new(nodeId:Int, code:String, message:String) {
		this.nodeId = nodeId;
		this.code = code;
		this.message = message;
	}
}
