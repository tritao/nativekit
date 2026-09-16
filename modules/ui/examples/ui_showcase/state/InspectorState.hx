package state;

/** Persistent selection and presentation state for the inspector drawer. */
class InspectorState {
	public var open:Bool = false;
	public var picking:Bool = false;
	public var width:Float = 270.0;
	public var tab:String = "preview";
	public var selectedNodeId:Int = 0;
	public var hoveredNodeId:Int = 0;
}
