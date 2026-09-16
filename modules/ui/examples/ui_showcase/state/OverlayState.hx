package state;

/** Visibility state for the showcase's modal and transient overlays. */
class OverlayState {
	public var dialogOpen:Bool = false;
	public var popupOpen:Bool = false;
	public var popupAnchorX:Float = -1.0;
	public var popupAnchorTop:Float = -1.0;
	public var popupAnchorBottom:Float = -1.0;
	public var menuOpen:Bool = false;
	public var menuAnchorX:Float = -1.0;
	public var menuAnchorTop:Float = -1.0;
	public var menuAnchorBottom:Float = -1.0;
}
